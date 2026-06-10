#pragma once

#include "net/NetTransport.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <string>

namespace lg {

class UdpServerTransport final : public NetTransport {
public:
  explicit UdpServerTransport(std::uint16_t port);
  ~UdpServerTransport() override;

  UdpServerTransport(const UdpServerTransport&) = delete;
  UdpServerTransport& operator=(const UdpServerTransport&) = delete;

  [[nodiscard]] bool initialize();
  void update();

  void sendCommand(const CommandPacket& packet) override;
  [[nodiscard]] bool receiveCommand(CommandPacket& packet) override;
  void sendSnapshot(const ServerSnapshot& snapshot) override;
  [[nodiscard]] bool receiveSnapshot(ServerSnapshot& snapshot) override;

  [[nodiscard]] std::uint16_t localPort() const;
  [[nodiscard]] std::size_t connectedClientCount() const;
  [[nodiscard]] std::array<bool, kDuelPlayerCount> connectedPlayers() const;
  [[nodiscard]] const std::string& lastError() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class UdpClientTransport final : public NetTransport {
public:
  UdpClientTransport(std::string host, std::uint16_t port);
  ~UdpClientTransport() override;

  UdpClientTransport(const UdpClientTransport&) = delete;
  UdpClientTransport& operator=(const UdpClientTransport&) = delete;

  [[nodiscard]] bool initialize();
  void disconnect();
  void update();

  void sendCommand(const CommandPacket& packet) override;
  [[nodiscard]] bool receiveCommand(CommandPacket& packet) override;
  void sendSnapshot(const ServerSnapshot& snapshot) override;
  [[nodiscard]] bool receiveSnapshot(ServerSnapshot& snapshot) override;

  [[nodiscard]] bool connected() const;
  [[nodiscard]] bool timedOut() const;
  [[nodiscard]] std::uint8_t playerIndex() const;
  [[nodiscard]] float pingMilliseconds() const;
  [[nodiscard]] const std::string& lastError() const;
  [[nodiscard]] const std::string& host() const;
  [[nodiscard]] std::uint16_t port() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace lg
