#include "dev/DevControlServer.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lg::dev {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle socket) { if (socket != kInvalidSocket) closesocket(socket); }
[[nodiscard]] std::string socketError() { return std::to_string(WSAGetLastError()); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle socket) { if (socket != kInvalidSocket) close(socket); }
[[nodiscard]] std::string socketError() { return std::to_string(errno); }
#endif

struct SharedRequest {
  std::uint64_t token = 0;
  ControlRequest request;
  std::mutex mutex;
  std::condition_variable changed;
  bool completed = false;
  JsonValue response;
};

[[nodiscard]] bool sendAll(SocketHandle socket, std::string_view text) {
  std::size_t sent = 0;
  while (sent < text.size()) {
    const std::size_t remaining = text.size() - sent;
    const int count = send(
      socket,
      text.data() + sent,
      static_cast<int>(std::min<std::size_t>(remaining, 16U * 1024U)),
      0
    );
    if (count <= 0) return false;
    sent += static_cast<std::size_t>(count);
  }
  return true;
}

[[nodiscard]] std::optional<std::string> receiveLine(SocketHandle socket, std::string& error) {
  std::string line;
  line.reserve(1024);
  char buffer[2048];
  for (;;) {
    const int count = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
    if (count <= 0) {
      error = line.empty() ? "connection closed before a request was received" : "request is missing a newline terminator";
      return std::nullopt;
    }
    const char* newline = static_cast<const char*>(
      std::memchr(buffer, '\n', static_cast<std::size_t>(count))
    );
    const std::size_t appendCount = newline == nullptr
      ? static_cast<std::size_t>(count)
      : static_cast<std::size_t>(newline - buffer);
    if (line.size() + appendCount > kMaxControlRequestBytes) {
      error = "request exceeds the 64 KiB control limit";
      return std::nullopt;
    }
    line.append(buffer, appendCount);
    if (newline != nullptr) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      return line;
    }
  }
}

} // namespace

struct DevControlServer::Impl {
  std::atomic<bool> active = false;
  std::uint16_t activePort = 0;
  SocketHandle listener = kInvalidSocket;
  std::jthread thread;
  mutable std::mutex queueMutex;
  std::deque<std::shared_ptr<SharedRequest>> queue;
  std::shared_ptr<SharedRequest> current;
  std::uint64_t nextToken = 1;
#ifdef _WIN32
  bool winsockStarted = false;
#endif

  void serveClient(SocketHandle client) {
#ifdef _WIN32
    const DWORD timeoutMilliseconds = 5000;
    (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
      reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
    (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
      reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
#else
    const timeval timeout = {5, 0};
    (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    std::string receiveError;
    const std::optional<std::string> line = receiveLine(client, receiveError);
    if (!line.has_value()) {
      (void)sendAll(client, writeJson(errorResponse("", "invalid_request", receiveError)) + "\n");
      return;
    }
    const JsonParseResult json = parseJson(*line);
    if (!json.ok) {
      (void)sendAll(client, writeJson(errorResponse("", "invalid_json", json.error)) + "\n");
      return;
    }
    const ControlRequestParseResult parsed = parseControlRequest(json.value);
    if (!parsed.ok) {
      const std::string id = stringMember(json.value, "id").value_or("");
      (void)sendAll(client, writeJson(errorResponse(id, "invalid_request", parsed.error)) + "\n");
      return;
    }

    auto pending = std::make_shared<SharedRequest>();
    pending->request = parsed.request;
    {
      std::lock_guard lock(queueMutex);
      pending->token = nextToken++;
      queue.push_back(pending);
      current = pending;
    }
    std::unique_lock lock(pending->mutex);
    const bool benchmarkRequest =
      pending->request.operation == ControlOperation::RunBenchmark;
    const auto completionTimeout = benchmarkRequest
      ? std::chrono::minutes(10)
      : std::chrono::minutes(1);
    const bool completed = pending->changed.wait_for(
      lock,
      completionTimeout,
      [&pending, this] { return pending->completed || !active.load(); }
    );
    JsonValue response;
    if (completed && pending->completed) {
      response = std::move(pending->response);
    } else {
      response = errorResponse(
        pending->request.id,
        "timeout",
        benchmarkRequest
          ? "game did not complete the benchmark request within 10 minutes"
          : "game did not complete the control request within 60 seconds"
      );
    }
    lock.unlock();
    (void)sendAll(client, writeJson(response) + "\n");
    std::lock_guard queueLock(queueMutex);
    if (current == pending) current.reset();
  }

  void run() {
    while (active.load()) {
      sockaddr_in address = {};
#ifdef _WIN32
      int addressSize = sizeof(address);
#else
      socklen_t addressSize = sizeof(address);
#endif
      const SocketHandle client = accept(
        listener,
        reinterpret_cast<sockaddr*>(&address),
        &addressSize
      );
      if (client == kInvalidSocket) {
        if (!active.load()) break;
        continue;
      }
      // Binding to INADDR_LOOPBACK is the primary boundary; this check guards
      // against platform/socket configuration regressions.
      if (ntohl(address.sin_addr.s_addr) != INADDR_LOOPBACK) {
        closeSocket(client);
        continue;
      }
      serveClient(client);
      closeSocket(client);
    }
  }
};

DevControlServer::DevControlServer() : impl_(std::make_unique<Impl>()) {}
DevControlServer::~DevControlServer() { stop(); }

bool DevControlServer::start(std::uint16_t port, std::string& error) {
  if (impl_->active.load()) {
    error = "developer control is already running";
    return false;
  }
#ifdef _WIN32
  WSADATA data = {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    error = "WSAStartup failed: " + socketError();
    return false;
  }
  impl_->winsockStarted = true;
#endif
  SocketHandle listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == kInvalidSocket) {
    error = "could not create control socket: " + socketError();
    stop();
    return false;
  }
#ifdef _WIN32
  const BOOL exclusive = TRUE;
  (void)setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
    reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
  const int reuse = 1;
  (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    error = "could not bind developer control to 127.0.0.1:" + std::to_string(port) + ": " + socketError();
    closeSocket(listener);
    stop();
    return false;
  }
  if (listen(listener, 4) != 0) {
    error = "could not listen on developer control socket: " + socketError();
    closeSocket(listener);
    stop();
    return false;
  }
  if (port == 0U) {
    sockaddr_in bound = {};
#ifdef _WIN32
    int boundSize = sizeof(bound);
#else
    socklen_t boundSize = sizeof(bound);
#endif
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &boundSize) != 0) {
      error = "could not query ephemeral developer-control port: " + socketError();
      closeSocket(listener);
      stop();
      return false;
    }
    port = ntohs(bound.sin_port);
  }
  impl_->listener = listener;
  impl_->activePort = port;
  impl_->active.store(true);
  impl_->thread = std::jthread([this] { impl_->run(); });
  return true;
}

void DevControlServer::stop() {
  const bool wasActive = impl_->active.exchange(false);
  if (wasActive) {
    closeSocket(impl_->listener);
    impl_->listener = kInvalidSocket;
    std::shared_ptr<SharedRequest> current;
    {
      std::lock_guard lock(impl_->queueMutex);
      current = impl_->current;
      impl_->queue.clear();
    }
    if (current != nullptr) current->changed.notify_all();
    if (impl_->thread.joinable()) impl_->thread.join();
  }
#ifdef _WIN32
  if (impl_->winsockStarted) {
    WSACleanup();
    impl_->winsockStarted = false;
  }
#endif
  impl_->activePort = 0;
}

bool DevControlServer::running() const { return impl_->active.load(); }
std::uint16_t DevControlServer::port() const { return impl_->activePort; }

std::optional<QueuedControlRequest> DevControlServer::pollRequest() {
  std::lock_guard lock(impl_->queueMutex);
  if (impl_->queue.empty()) return std::nullopt;
  std::shared_ptr<SharedRequest> pending = std::move(impl_->queue.front());
  impl_->queue.pop_front();
  return QueuedControlRequest{pending->token, pending->request};
}

void DevControlServer::complete(std::uint64_t token, JsonValue response) {
  std::shared_ptr<SharedRequest> pending;
  {
    std::lock_guard lock(impl_->queueMutex);
    if (impl_->current != nullptr && impl_->current->token == token) pending = impl_->current;
  }
  if (pending == nullptr) return;
  {
    std::lock_guard lock(pending->mutex);
    pending->response = std::move(response);
    pending->completed = true;
  }
  pending->changed.notify_one();
}

} // namespace lg::dev
