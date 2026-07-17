#pragma once

#include "dev/DevControlProtocol.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace lg::dev {

struct QueuedControlRequest {
  std::uint64_t token = 0;
  ControlRequest request;
};

// The socket thread only parses and queues bounded requests. Game and renderer
// state is observed or changed by pollRequest() on the client main thread.
class DevControlServer {
public:
  DevControlServer();
  DevControlServer(const DevControlServer&) = delete;
  DevControlServer& operator=(const DevControlServer&) = delete;
  ~DevControlServer();

  [[nodiscard]] bool start(std::uint16_t port, std::string& error);
  void stop();
  [[nodiscard]] bool running() const;
  [[nodiscard]] std::uint16_t port() const;
  [[nodiscard]] std::optional<QueuedControlRequest> pollRequest();
  void complete(std::uint64_t token, JsonValue response);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace lg::dev
