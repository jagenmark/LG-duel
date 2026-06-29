#include "net/UdpTransport.hpp"

#include "net/NetCodec.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <vector>
#include <string>
#include <utility>

  #if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lg {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kHandshakeRetry = std::chrono::milliseconds(500);
constexpr auto kPingInterval = std::chrono::seconds(1);
constexpr auto kConnectionTimeout = std::chrono::seconds(1);

#if defined(_WIN32)
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

struct SocketRuntime {
  SocketRuntime() {
    WSADATA data;
    valid = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }

  ~SocketRuntime() {
    if (valid) {
      WSACleanup();
    }
  }

  bool valid = false;
};

void closeSocket(SocketHandle socket) {
  if (socket != kInvalidSocket) {
    closesocket(socket);
  }
}

bool wouldBlock() {
  const int error = WSAGetLastError();
  return error == WSAEWOULDBLOCK;
}

bool setNonBlocking(SocketHandle socket) {
  u_long enabled = 1;
  return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;

struct SocketRuntime {
  bool valid = true;
};

void closeSocket(SocketHandle socket) {
  if (socket != kInvalidSocket) {
    close(socket);
  }
}

bool wouldBlock() {
  return errno == EWOULDBLOCK || errno == EAGAIN;
}

bool setNonBlocking(SocketHandle socket) {
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

struct Endpoint {
  sockaddr_storage address = {};
  SocketLength length = 0;
};

bool sameEndpoint(const Endpoint& lhs, const Endpoint& rhs) {
  if (lhs.address.ss_family != rhs.address.ss_family) {
    return false;
  }

  if (lhs.address.ss_family == AF_INET) {
    const auto* first = reinterpret_cast<const sockaddr_in*>(&lhs.address);
    const auto* second = reinterpret_cast<const sockaddr_in*>(&rhs.address);
    return first->sin_port == second->sin_port &&
      first->sin_addr.s_addr == second->sin_addr.s_addr;
  }

  if (lhs.address.ss_family == AF_INET6) {
    const auto* first = reinterpret_cast<const sockaddr_in6*>(&lhs.address);
    const auto* second = reinterpret_cast<const sockaddr_in6*>(&rhs.address);
    return first->sin6_port == second->sin6_port &&
      first->sin6_scope_id == second->sin6_scope_id &&
      std::memcmp(&first->sin6_addr, &second->sin6_addr, sizeof(in6_addr)) == 0;
  }

  return false;
}

bool sendWire(SocketHandle socket, const Endpoint& endpoint, const WirePacket& wire) {
  if (socket == kInvalidSocket || wire.empty()) {
    return false;
  }
  const int sent = sendto(
    socket,
    reinterpret_cast<const char*>(wire.data()),
    static_cast<int>(wire.size()),
    0,
    reinterpret_cast<const sockaddr*>(&endpoint.address),
    endpoint.length
  );
  return sent == static_cast<int>(wire.size());
}

bool receiveWire(SocketHandle socket, Endpoint& endpoint, WirePacket& wire) {
  std::array<std::uint8_t, kMaxPacketBytes + 1> buffer = {};
  endpoint.length = sizeof(endpoint.address);
  const int received = recvfrom(
    socket,
    reinterpret_cast<char*>(buffer.data()),
    static_cast<int>(buffer.size()),
    0,
    reinterpret_cast<sockaddr*>(&endpoint.address),
    &endpoint.length
  );
  if (received < 0) {
    return false;
  }
  if (received == 0 || static_cast<std::size_t>(received) > kMaxPacketBytes) {
    wire.clear();
    return true;
  }
  wire.assign(buffer.begin(), buffer.begin() + received);
  return true;
}

std::uint16_t boundPort(SocketHandle socket) {
  sockaddr_storage address = {};
  SocketLength length = sizeof(address);
  if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    return 0;
  }
  if (address.ss_family == AF_INET) {
    return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
  }
  if (address.ss_family == AF_INET6) {
    return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
  }
  return 0;
}

} // namespace

struct UdpServerTransport::Impl {
  struct ClientSlot {
    bool active = false;
    Endpoint endpoint = {};
    std::uint32_t nonce = 0;
    std::uint32_t session = 0;
    Clock::time_point lastHeard = {};
    std::uint32_t lastFullArenaRevision = 0;
    std::uint32_t lastFullArenaTick = 0;
  };

  explicit Impl(std::uint16_t requestedPort) : port(requestedPort) {}

  void pump() {
    if (socket == kInvalidSocket) {
      return;
    }

    while (true) {
      Endpoint sender;
      WirePacket wire;
      if (!receiveWire(socket, sender, wire)) {
        if (wouldBlock()) {
          break;
        }
        break;
      }
      if (wire.empty()) {
        continue;
      }

      PacketType type;
      if (!inspectPacketType(wire, type)) {
        continue;
      }
      if (type == PacketType::ConnectRequest) {
        ConnectRequest request;
        if (!decodeConnectRequest(wire, request)) {
          continue;
        }

        std::size_t slotIndex = kDuelPlayerCount;
        for (std::size_t index = 0; index < clients.size(); ++index) {
          if (clients[index].active && sameEndpoint(clients[index].endpoint, sender)) {
            slotIndex = index;
            break;
          }
        }
        if (slotIndex == kDuelPlayerCount) {
          for (std::size_t index = 0; index < clients.size(); ++index) {
            if (!clients[index].active) {
              slotIndex = index;
              clients[index].active = true;
              clients[index].endpoint = sender;
              break;
            }
          }
        }
        if (slotIndex == kDuelPlayerCount) {
          continue;
        }

        if (clients[slotIndex].session == 0 ||
            clients[slotIndex].nonce != request.clientNonce) {
          clients[slotIndex].nonce = request.clientNonce;
          clients[slotIndex].session = nextSession++;
          if (nextSession == 0) {
            nextSession = 1;
          }
          clients[slotIndex].lastFullArenaRevision = 0;
          clients[slotIndex].lastFullArenaTick = 0;
        }
        clients[slotIndex].lastHeard = Clock::now();
        WirePacket response;
        if (encodeConnectAccept(
          ConnectAccept{
            request.clientNonce,
            static_cast<std::uint8_t>(slotIndex),
            lastServerTick,
          },
          response
        )) {
          sendWire(socket, sender, response);
        }
        continue;
      }

      const std::size_t slotIndex = findClient(sender);
      if (slotIndex == kDuelPlayerCount) {
        continue;
      }
      clients[slotIndex].lastHeard = Clock::now();

      if (type == PacketType::CommandBundle) {
        CommandBundle bundle;
        if (!decodeCommandBundle(wire, bundle)) {
          continue;
        }
        for (std::size_t index = 0; index < bundle.commandCount; ++index) {
          if (
            bundle.commands[index].playerIndex == slotIndex &&
            bundle.commands[index].clientNonce == clients[slotIndex].nonce
          ) {
            commands.push_back(bundle.commands[index]);
          }
        }
      } else if (type == PacketType::Command) {
        CommandPacket packet;
        if (
          decodeCommandPacket(wire, packet) &&
          packet.playerIndex == slotIndex &&
          packet.clientNonce == clients[slotIndex].nonce
        ) {
          commands.push_back(packet);
        }
      } else if (type == PacketType::Ping) {
        PingPacket ping;
        WirePacket pong;
        if (
          decodePingPacket(wire, PacketType::Ping, ping) &&
          encodePingPacket(PacketType::Pong, ping, pong)
        ) {
          sendWire(socket, sender, pong);
        }
      } else if (type == PacketType::Disconnect) {
        DisconnectPacket packet;
        if (
          decodeDisconnectPacket(wire, packet) &&
          packet.clientNonce == clients[slotIndex].nonce
        ) {
          clients[slotIndex] = {};
        }
      }
    }

    const auto now = Clock::now();
    for (ClientSlot& client : clients) {
      if (client.active && now - client.lastHeard > kConnectionTimeout) {
        client = {};
      }
    }
  }

  std::size_t findClient(const Endpoint& endpoint) const {
    for (std::size_t index = 0; index < clients.size(); ++index) {
      if (clients[index].active && sameEndpoint(clients[index].endpoint, endpoint)) {
        return index;
      }
    }
    return kDuelPlayerCount;
  }

  SocketRuntime runtime;
  std::uint16_t port = 0;
  SocketHandle socket = kInvalidSocket;
  std::array<ClientSlot, kDuelPlayerCount> clients = {};
  std::deque<CommandPacket> commands;
  std::uint32_t lastServerTick = 0;
  std::uint32_t nextSession = 1;
  std::string error;
};

UdpServerTransport::UdpServerTransport(std::uint16_t port)
  : impl_(std::make_unique<Impl>(port)) {}

UdpServerTransport::~UdpServerTransport() {
  closeSocket(impl_->socket);
}

bool UdpServerTransport::initialize() {
  if (!impl_->runtime.valid) {
    impl_->error = "socket runtime initialization failed";
    return false;
  }

  impl_->socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (impl_->socket != kInvalidSocket) {
    int dualStack = 0;
    setsockopt(
      impl_->socket,
      IPPROTO_IPV6,
      IPV6_V6ONLY,
      reinterpret_cast<const char*>(&dualStack),
      sizeof(dualStack)
    );

    sockaddr_in6 address = {};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    address.sin6_port = htons(impl_->port);
    if (
      bind(
        impl_->socket,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
      ) != 0
    ) {
      closeSocket(impl_->socket);
      impl_->socket = kInvalidSocket;
    }
  }

  if (impl_->socket == kInvalidSocket) {
    impl_->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(impl_->port);
    if (
      impl_->socket == kInvalidSocket ||
      bind(
        impl_->socket,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
      ) != 0
    ) {
      impl_->error = "socket creation or bind failed";
      closeSocket(impl_->socket);
      impl_->socket = kInvalidSocket;
      return false;
    }
  }

  if (!setNonBlocking(impl_->socket)) {
    impl_->error = "socket nonblocking setup failed";
    closeSocket(impl_->socket);
    impl_->socket = kInvalidSocket;
    return false;
  }

  impl_->port = boundPort(impl_->socket);
  return true;
}

void UdpServerTransport::update() {
  impl_->pump();
}

void UdpServerTransport::sendCommand(const CommandPacket&) {}

bool UdpServerTransport::receiveCommand(CommandPacket& packet) {
  impl_->pump();
  if (impl_->commands.empty()) {
    return false;
  }
  packet = impl_->commands.front();
  impl_->commands.pop_front();
  return true;
}

void UdpServerTransport::sendSnapshot(const ServerSnapshot& snapshot) {
  impl_->lastServerTick = snapshot.serverTick;
  constexpr std::uint32_t kFullArenaSnapshotIntervalTicks = 125;
  for (Impl::ClientSlot& client : impl_->clients) {
    if (!client.active) {
      continue;
    }

    ServerSnapshot outgoing = snapshot;
    outgoing.hasArena =
      client.lastFullArenaRevision != snapshot.mapRevision ||
      snapshot.serverTick - client.lastFullArenaTick >= kFullArenaSnapshotIntervalTicks;

    WirePacket wire;
    if (!encodeServerSnapshot(outgoing, wire)) {
      continue;
    }
    if (sendWire(impl_->socket, client.endpoint, wire) && outgoing.hasArena) {
      client.lastFullArenaRevision = snapshot.mapRevision;
      client.lastFullArenaTick = snapshot.serverTick;
    }
  }
}

bool UdpServerTransport::receiveSnapshot(ServerSnapshot&) {
  return false;
}

std::uint16_t UdpServerTransport::localPort() const {
  return impl_->port;
}

std::size_t UdpServerTransport::connectedClientCount() const {
  return static_cast<std::size_t>(std::count_if(
    impl_->clients.begin(),
    impl_->clients.end(),
    [](const Impl::ClientSlot& client) { return client.active; }
  ));
}

std::array<bool, kDuelPlayerCount> UdpServerTransport::connectedPlayers() const {
  std::array<bool, kDuelPlayerCount> connected = {};
  for (std::size_t index = 0; index < impl_->clients.size(); ++index) {
    connected[index] = impl_->clients[index].active;
  }
  return connected;
}

std::array<std::uint32_t, kDuelPlayerCount>
UdpServerTransport::connectedPlayerSessions() const {
  std::array<std::uint32_t, kDuelPlayerCount> sessions = {};
  for (std::size_t index = 0; index < impl_->clients.size(); ++index) {
    if (impl_->clients[index].active) {
      sessions[index] = impl_->clients[index].session;
    }
  }
  return sessions;
}

const std::string& UdpServerTransport::lastError() const {
  return impl_->error;
}

struct UdpClientTransport::Impl {
  Impl(std::string serverHost, std::uint16_t serverPort)
    : host(std::move(serverHost)), port(serverPort) {}

  void sendConnectedWire(const WirePacket& wire, Clock::time_point now) {
    if (!connected || !networkSim.active()) {
      sendWire(socket, server, wire);
      return;
    }
    if (
      networkSim.enqueue(ClientNetworkSimDirection::Outgoing, wire, now) ==
      ClientNetworkSimAction::Immediate
    ) {
      sendWire(socket, server, wire);
    }
  }

  void flushOutgoing(Clock::time_point now) {
    WirePacket wire;
    while (networkSim.popDue(ClientNetworkSimDirection::Outgoing, now, wire)) {
      sendWire(socket, server, wire);
    }
  }

  void dispatchWire(const WirePacket& wire, Clock::time_point now) {
    PacketType type;
    if (!inspectPacketType(wire, type)) {
      return;
    }
    if (type == PacketType::Snapshot && connected) {
      ServerSnapshot snapshot;
      if (decodeServerSnapshot(wire, snapshot)) {
        snapshots.push_back(snapshot);
        lastServerPacket = now;
      }
    } else if (type == PacketType::Pong && connected) {
      PingPacket pong;
      if (
        decodePingPacket(wire, PacketType::Pong, pong) &&
        pong.token == pingToken
      ) {
        pingMs = std::chrono::duration<float, std::milli>(
          now - pingSentAt
        ).count();
        lastServerPacket = now;
      }
    }
  }

  void flushIncoming(Clock::time_point now) {
    WirePacket wire;
    while (networkSim.popDue(ClientNetworkSimDirection::Incoming, now, wire)) {
      dispatchWire(wire, now);
    }
  }

  void sendConnect() {
    WirePacket wire;
    if (encodeConnectRequest(ConnectRequest{nonce}, wire)) {
      sendWire(socket, server, wire);
      lastHandshakeSend = Clock::now();
    }
  }

  void pump() {
    if (socket == kInvalidSocket) {
      return;
    }

    const auto now = Clock::now();
    if (!connected && now - lastHandshakeSend >= kHandshakeRetry) {
      sendConnect();
    }
    if (connected && now - lastPingSend >= kPingInterval) {
      ++pingToken;
      WirePacket wire;
      if (encodePingPacket(PacketType::Ping, PingPacket{pingToken}, wire)) {
        sendConnectedWire(wire, now);
        pingSentAt = now;
        lastPingSend = now;
      }
    }
    flushOutgoing(now);

    while (true) {
      Endpoint sender;
      WirePacket wire;
      if (!receiveWire(socket, sender, wire)) {
        if (wouldBlock()) {
          break;
        }
        break;
      }
      if (wire.empty() || !sameEndpoint(server, sender)) {
        continue;
      }

      PacketType type;
      if (!inspectPacketType(wire, type)) {
        continue;
      }
      if (type == PacketType::ConnectAccept) {
        ConnectAccept accept;
        if (decodeConnectAccept(wire, accept) && accept.clientNonce == nonce) {
          connected = true;
          timedOut = false;
          assignedPlayer = accept.playerIndex;
          lastServerPacket = Clock::now();
          lastPingSend = Clock::now() - kPingInterval;
        }
      } else if (connected) {
        if (
          networkSim.active() &&
          networkSim.enqueue(ClientNetworkSimDirection::Incoming, wire, now) !=
            ClientNetworkSimAction::Immediate
        ) {
          continue;
        }
        dispatchWire(wire, now);
      }
    }
    flushIncoming(Clock::now());

    if (connected && Clock::now() - lastServerPacket > kConnectionTimeout) {
      connected = false;
      timedOut = true;
      commandHistory.clear();
      networkSim.clear();
    }
  }

  SocketRuntime runtime;
  std::string host;
  std::uint16_t port = 0;
  SocketHandle socket = kInvalidSocket;
  Endpoint server = {};
  std::deque<ServerSnapshot> snapshots;
  std::deque<CommandPacket> commandHistory;
  ClientNetworkSimulator networkSim;
  std::uint32_t nonce = 0;
  std::uint32_t pingToken = 0;
  std::uint8_t assignedPlayer = 0;
  bool connected = false;
  bool timedOut = false;
  float pingMs = 0.0F;
  Clock::time_point lastHandshakeSend = Clock::time_point::min();
  Clock::time_point lastPingSend = Clock::time_point::min();
  Clock::time_point pingSentAt = {};
  Clock::time_point lastServerPacket = {};
  std::string error;
};

UdpClientTransport::UdpClientTransport(std::string host, std::uint16_t port)
  : impl_(std::make_unique<Impl>(std::move(host), port)) {}

UdpClientTransport::~UdpClientTransport() {
  disconnect();
  closeSocket(impl_->socket);
}

bool UdpClientTransport::initialize() {
  if (!impl_->runtime.valid) {
    impl_->error = "socket runtime initialization failed";
    return false;
  }

  addrinfo hints = {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  addrinfo* results = nullptr;
  const std::string portText = std::to_string(impl_->port);
  if (getaddrinfo(impl_->host.c_str(), portText.c_str(), &hints, &results) != 0) {
    impl_->error = "server address resolution failed";
    return false;
  }

  for (addrinfo* result = results; result != nullptr; result = result->ai_next) {
    const SocketHandle candidate = socket(
      result->ai_family,
      result->ai_socktype,
      result->ai_protocol
    );
    if (candidate == kInvalidSocket || !setNonBlocking(candidate)) {
      closeSocket(candidate);
      continue;
    }

    impl_->socket = candidate;
    std::memcpy(&impl_->server.address, result->ai_addr, result->ai_addrlen);
    impl_->server.length = static_cast<SocketLength>(result->ai_addrlen);
    break;
  }
  freeaddrinfo(results);

  if (impl_->socket == kInvalidSocket) {
    impl_->error = "UDP socket creation failed";
    return false;
  }

  const auto timestamp = Clock::now().time_since_epoch().count();
  impl_->nonce = static_cast<std::uint32_t>(timestamp) ^
    static_cast<std::uint32_t>(timestamp >> 32U);
  if (impl_->nonce == 0) {
    impl_->nonce = 1;
  }
  impl_->sendConnect();
  return true;
}

void UdpClientTransport::disconnect() {
  if (impl_->socket == kInvalidSocket) {
    return;
  }
  if (impl_->connected) {
    WirePacket wire;
    if (encodeDisconnectPacket(DisconnectPacket{impl_->nonce}, wire)) {
      sendWire(impl_->socket, impl_->server, wire);
    }
  }
  impl_->connected = false;
  impl_->timedOut = false;
  impl_->commandHistory.clear();
  impl_->snapshots.clear();
}

void UdpClientTransport::update() {
  impl_->pump();
}

void UdpClientTransport::setNetworkSimulationConfig(
  const ClientNetworkSimulationConfig& config
) {
  impl_->networkSim.setConfig(config);
}

void UdpClientTransport::sendCommand(const CommandPacket& packet) {
  impl_->pump();
  if (!impl_->connected) {
    return;
  }

  CommandPacket stampedPacket = packet;
  stampedPacket.clientNonce = impl_->nonce;
  impl_->commandHistory.push_back(stampedPacket);
  while (impl_->commandHistory.size() > kMaxBundledCommands) {
    impl_->commandHistory.pop_front();
  }

  CommandBundle bundle;
  bundle.commandCount = static_cast<std::uint8_t>(impl_->commandHistory.size());
  std::size_t index = 0;
  for (const CommandPacket& command : impl_->commandHistory) {
    bundle.commands[index++] = command;
  }

  WirePacket wire;
  if (encodeCommandBundle(bundle, wire)) {
    impl_->sendConnectedWire(wire, Clock::now());
  }
}

bool UdpClientTransport::receiveCommand(CommandPacket&) {
  return false;
}

void UdpClientTransport::sendSnapshot(const ServerSnapshot&) {}

bool UdpClientTransport::receiveSnapshot(ServerSnapshot& snapshot) {
  impl_->pump();
  if (impl_->snapshots.empty()) {
    return false;
  }
  snapshot = impl_->snapshots.front();
  impl_->snapshots.pop_front();
  return true;
}

bool UdpClientTransport::connected() const {
  return impl_->connected;
}

bool UdpClientTransport::timedOut() const {
  return impl_->timedOut;
}

std::uint8_t UdpClientTransport::playerIndex() const {
  return impl_->assignedPlayer;
}

float UdpClientTransport::pingMilliseconds() const {
  return impl_->pingMs;
}

ClientNetworkSimulationStats UdpClientTransport::networkSimulationStats() const {
  return impl_->networkSim.stats();
}

ClientNetworkSimulationConfig UdpClientTransport::networkSimulationConfig() const {
  return impl_->networkSim.config();
}

const std::string& UdpClientTransport::lastError() const {
  return impl_->error;
}

const std::string& UdpClientTransport::host() const {
  return impl_->host;
}

std::uint16_t UdpClientTransport::port() const {
  return impl_->port;
}

} // namespace lg
