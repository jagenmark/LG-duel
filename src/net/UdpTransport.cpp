#include "net/UdpTransport.hpp"

#include "net/NetCodec.hpp"
#include "shared/Sequence.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
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

// The simulation runs at 125 Hz, so 25 ticks keeps scoreboard data at 5 Hz.
constexpr std::uint32_t kCombatStatsRefreshTicks = 25;
constexpr std::size_t kCombatStatsPlayersPerPacket = 4;

void copySnapshotConfiguration(ServerSnapshot& destination,
                               const ServerSnapshot& source) {
  destination.matchRules = source.matchRules;
  destination.movementTuning = source.movementTuning;
  destination.playerSizeScaleXY = source.playerSizeScaleXY;
  destination.playerSizeScaleZ = source.playerSizeScaleZ;
  destination.lightningKnockback = source.lightningKnockback;
  destination.lightningFireHz = source.lightningFireHz;
  destination.rocketKnockback = source.rocketKnockback;
  destination.knockbackTimeMs = source.knockbackTimeMs;
  destination.weaponDamage = source.weaponDamage;
  destination.icePoolTuning = source.icePoolTuning;
  destination.projectilePresentation = source.projectilePresentation;
  destination.weaponAmmo = source.weaponAmmo;
  destination.vampirism = source.vampirism;
  destination.selfDamagePercent = source.selfDamagePercent;
  destination.healthAmount = source.healthAmount;
  destination.weaponSwitchingMode = source.weaponSwitchingMode;
  destination.mcguffinConfig = source.mcguffinConfig;
}

void copySnapshotPlayerNames(ServerSnapshot& destination,
                             const ServerSnapshot& source) {
  destination.playerNames = source.playerNames;
}

WirePacket configurationSignature(const ServerSnapshot& snapshot) {
  ServerSnapshot canonical;
  canonical.map = {"config", 1U};
  canonical.hasCombatStats = false;
  canonical.hasPlayerNames = false;
  copySnapshotConfiguration(canonical, snapshot);
  WirePacket signature;
  if (!encodeServerSnapshot(canonical, signature)) {
    signature.clear();
  }
  return signature;
}

using Clock = std::chrono::steady_clock;
constexpr auto kHandshakeRetry = std::chrono::milliseconds(500);
constexpr auto kPingInterval = std::chrono::seconds(1);
constexpr auto kConnectionTimeout = std::chrono::seconds(15);
constexpr auto kChatRetransmitInterval = std::chrono::milliseconds(100);

void recordReceivedSequence(
  std::uint32_t sequence,
  std::uint32_t& latest,
  std::uint32_t& previousBits
) {
  if (sequence == 0 || sequence == latest) return;
  if (latest == 0) {
    latest = sequence;
    previousBits = 0;
    return;
  }
  if (isSequenceNewer(sequence, latest)) {
    const std::uint32_t distance = sequence - latest;
    previousBits = distance >= 32U ? 0U : previousBits << distance;
    if (distance <= 32U) previousBits |= 1U << (distance - 1U);
    latest = sequence;
    return;
  }
  const std::uint32_t distance = latest - sequence;
  if (distance >= 1U && distance <= 32U) {
    previousBits |= 1U << (distance - 1U);
  }
}

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
  // The application ceiling is below common path MTUs. Never make correctness
  // depend on IP fragmentation, even if a codec regression produces a packet.
  if (socket == kInvalidSocket || wire.empty() ||
      wire.size() > kMaxUdpApplicationDatagramBytes) {
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
  // The extra byte detects a peer attempting to bypass the application MTU.
  // Receive and discard the whole datagram, but never pass it to a decoder.
  std::array<std::uint8_t, kMaxUdpApplicationDatagramBytes + 1> buffer = {};
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
#if defined(_WIN32)
    // Winsock reports an oversized datagram as consumed plus WSAEMSGSIZE.
    // Surface it as an empty invalid packet so the pump continues to later
    // legitimate traffic instead of letting an MTU violation starve the queue.
    if (WSAGetLastError() == WSAEMSGSIZE) {
      wire.clear();
      return true;
    }
#endif
    return false;
  }
  if (received == 0 ||
      static_cast<std::size_t>(received) > kMaxUdpApplicationDatagramBytes) {
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
    std::uint32_t chatAckSequence = 0;
    std::uint32_t acknowledgedConfigurationRevision = 0;
    std::uint32_t acknowledgedPlayerNameRevision = 0;
    std::uint32_t latestCommandDatagramSequence = 0;
    std::uint32_t commandDatagramAckBits = 0;
    std::uint32_t lastCombatStatsTick = 0;
    bool wantsScoreboardStats = false;
    std::uint8_t playerIndex = kNoAssignedPlayer;
    Clock::time_point lastChatSend = Clock::time_point::min();
  };

  explicit Impl(std::uint16_t requestedPort) : port(requestedPort) {}

  std::uint8_t availablePlayerIndex() const {
    for (std::size_t player = 0; player < kDuelPlayerCount; ++player) {
      const bool assigned = std::any_of(
        clients.begin(), clients.end(),
        [player](const ClientSlot& client) {
          return client.active && client.playerIndex == player;
        }
      );
      if (!assigned) return static_cast<std::uint8_t>(player);
    }
    return kNoAssignedPlayer;
  }

  std::uint8_t spectatorCount() const {
    return static_cast<std::uint8_t>(std::count_if(
      clients.begin(), clients.end(),
      [](const ClientSlot& client) {
        return client.active && client.playerIndex == kNoAssignedPlayer;
      }
    ));
  }

  bool translateCommand(std::size_t clientIndex, CommandPacket& packet) {
    ClientSlot& client = clients[clientIndex];
    if (packet.requestSpectator) {
      if (client.playerIndex != kNoAssignedPlayer &&
          spectatorCount() >= kMaxSpectatorClients) {
        packet.playerIndex = client.playerIndex;
        return true;
      }
      // Releasing the mapping removes the authoritative body on the next
      // server tick; the connection itself remains available for snapshots.
      client.playerIndex = kNoAssignedPlayer;
      return false;
    }
    const bool assignmentOpen =
      lastMatchPhase == MatchPhase::WaitingForPlayers ||
      lastMatchPhase == MatchPhase::WaitingForReady;
    if (client.playerIndex == kNoAssignedPlayer && packet.requestTeam &&
        assignmentOpen) {
      client.playerIndex = availablePlayerIndex();
    }
    if (client.playerIndex == kNoAssignedPlayer) {
      // Chat belongs to the authenticated connection, not to a collision body.
      // Preserve the sentinel so ServerGame can publish spectator chat without
      // granting any body-authoritative command capability.
      packet.playerIndex = kNoAssignedPlayer;
      return !packet.chatMessage.empty();
    }
    packet.playerIndex = client.playerIndex;
    return true;
  }

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

        std::size_t slotIndex = clients.size();
        for (std::size_t index = 0; index < clients.size(); ++index) {
          if (clients[index].active && sameEndpoint(clients[index].endpoint, sender)) {
            slotIndex = index;
            break;
          }
        }
        if (slotIndex == clients.size()) {
          for (std::size_t index = 0; index < clients.size(); ++index) {
            if (!clients[index].active) {
              slotIndex = index;
              clients[index].active = true;
              clients[index].endpoint = sender;
              clients[index].playerIndex = availablePlayerIndex();
              break;
            }
          }
        }
        if (slotIndex == clients.size()) {
          continue;
        }

        if (clients[slotIndex].session == 0 ||
            clients[slotIndex].nonce != request.clientNonce) {
          // A changed nonce from the same endpoint starts a new logical client
          // session and invalidates map-transfer state left by the old process.
          clients[slotIndex].nonce = request.clientNonce;
          clients[slotIndex].session = nextSession++;
          if (nextSession == 0) {
            nextSession = 1;
          }
          clients[slotIndex].lastFullArenaRevision = 0;
          clients[slotIndex].lastFullArenaTick = 0;
          clients[slotIndex].chatAckSequence = 0;
          clients[slotIndex].acknowledgedConfigurationRevision = 0;
          clients[slotIndex].acknowledgedPlayerNameRevision = 0;
          clients[slotIndex].latestCommandDatagramSequence = 0;
          clients[slotIndex].commandDatagramAckBits = 0;
          clients[slotIndex].lastCombatStatsTick = 0;
          clients[slotIndex].wantsScoreboardStats = false;
          clients[slotIndex].lastChatSend = Clock::time_point::min();
        }
        clients[slotIndex].lastHeard = Clock::now();
        WirePacket response;
        if (encodeConnectAccept(
          ConnectAccept{
            request.clientNonce,
            static_cast<std::uint8_t>(slotIndex),
            clients[slotIndex].playerIndex,
            lastServerTick,
          },
          response
        )) {
          sendWire(socket, sender, response);
        }
        continue;
      }

      const std::size_t slotIndex = findClient(sender);
      if (slotIndex == clients.size()) {
        continue;
      }
      clients[slotIndex].lastHeard = Clock::now();

      if (type == PacketType::CommandBundle) {
        CommandBundle bundle;
        if (!decodeCommandBundle(wire, bundle)) {
          continue;
        }
        bool acceptedBundle = false;
        for (std::size_t index = 0; index < bundle.commandCount; ++index) {
          if (
            bundle.commands[index].clientIndex == slotIndex &&
            bundle.commands[index].clientNonce == clients[slotIndex].nonce
          ) {
            acceptedBundle = true;
            if (bundle.commands[index].acknowledgedConfigurationRevision <=
                configurationRevision) {
              clients[slotIndex].acknowledgedConfigurationRevision =
                bundle.commands[index].acknowledgedConfigurationRevision;
            }
            if (bundle.commands[index].acknowledgedPlayerNameRevision ==
                playerNameRevision) {
              clients[slotIndex].acknowledgedPlayerNameRevision =
                playerNameRevision;
            }
            if (bundle.commands[index].wantsScoreboardStats &&
                !clients[slotIndex].wantsScoreboardStats) {
              // Interest is repeated for loss tolerance; only the opening edge
              // resets the timer so the first statistics packet is immediate.
              clients[slotIndex].lastCombatStatsTick = 0;
            }
            clients[slotIndex].wantsScoreboardStats =
              bundle.commands[index].wantsScoreboardStats;
            CommandPacket translated = bundle.commands[index];
            if (translateCommand(slotIndex, translated)) {
              commands.push_back(std::move(translated));
            }
          }
        }
        if (acceptedBundle) {
          recordReceivedSequence(
            bundle.datagramSequence,
            clients[slotIndex].latestCommandDatagramSequence,
            clients[slotIndex].commandDatagramAckBits
          );
        }
      } else if (type == PacketType::Command) {
        CommandPacket packet;
        if (
          decodeCommandPacket(wire, packet) &&
          packet.clientIndex == slotIndex &&
          packet.clientNonce == clients[slotIndex].nonce
        ) {
          if (packet.acknowledgedConfigurationRevision <= configurationRevision) {
            clients[slotIndex].acknowledgedConfigurationRevision =
              packet.acknowledgedConfigurationRevision;
          }
          if (packet.acknowledgedPlayerNameRevision == playerNameRevision) {
            clients[slotIndex].acknowledgedPlayerNameRevision =
              playerNameRevision;
          }
          if (packet.wantsScoreboardStats &&
              !clients[slotIndex].wantsScoreboardStats) {
            clients[slotIndex].lastCombatStatsTick = 0;
          }
          clients[slotIndex].wantsScoreboardStats = packet.wantsScoreboardStats;
          if (translateCommand(slotIndex, packet)) {
            commands.push_back(std::move(packet));
          }
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
      } else if (type == PacketType::ChatHistoryAck) {
        ChatHistoryAck ack;
        if (decodeChatHistoryAck(wire, ack)) {
          clients[slotIndex].chatAckSequence = ack.sequence;
          // An acknowledgement opens the next chunk immediately; the interval
          // only throttles retransmission when an acknowledgement is lost.
          clients[slotIndex].lastChatSend = Clock::time_point::min();
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
    return clients.size();
  }

  SocketRuntime runtime;
  std::uint16_t port = 0;
  SocketHandle socket = kInvalidSocket;
  std::array<ClientSlot, kMaxNetworkClients> clients = {};
  std::deque<CommandPacket> commands;
  std::uint32_t lastServerTick = 0;
  MatchPhase lastMatchPhase = MatchPhase::WaitingForPlayers;
  std::uint32_t nextSession = 1;
  ChatHistory chatHistory = {};
  WirePacket configurationBytes;
  std::uint32_t configurationRevision = 0;
  std::array<std::string, kDuelPlayerCount> playerNameValues = {};
  std::uint32_t playerNameRevision = 0;
  bool hasPlayerNameValues = false;
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
  // update() is the sole socket pump so roster/session mappings are published
  // to ServerGame before commands from that same batch can be consumed.
  while (!impl_->commands.empty()) {
    CommandPacket candidate = std::move(impl_->commands.front());
    impl_->commands.pop_front();
    const std::size_t clientIndex = candidate.clientIndex;
    if (
      clientIndex >= impl_->clients.size() ||
      !impl_->clients[clientIndex].active ||
      impl_->clients[clientIndex].nonce != candidate.clientNonce ||
      impl_->clients[clientIndex].playerIndex != candidate.playerIndex
    ) {
      // A queued packet belongs to an old nonce or body assignment. Dropping it
      // prevents stale high sequences from poisoning a newly assigned body.
      continue;
    }
    packet = std::move(candidate);
    return true;
  }
  return false;
}

void UdpServerTransport::sendSnapshot(const ServerSnapshot& snapshot) {
  impl_->lastServerTick = snapshot.serverTick;
  impl_->lastMatchPhase = snapshot.matchPhase;
  const WirePacket currentConfiguration = configurationSignature(snapshot);
  if (currentConfiguration != impl_->configurationBytes) {
    impl_->configurationBytes = currentConfiguration;
    ++impl_->configurationRevision;
    if (impl_->configurationRevision == 0) impl_->configurationRevision = 1;
  }
  if (!impl_->hasPlayerNameValues ||
      snapshot.playerNames != impl_->playerNameValues) {
    impl_->playerNameValues = snapshot.playerNames;
    impl_->hasPlayerNameValues = true;
    ++impl_->playerNameRevision;
    if (impl_->playerNameRevision == 0U) impl_->playerNameRevision = 1U;
  }
  const std::uint8_t spectatorCount = impl_->spectatorCount();
  for (Impl::ClientSlot& client : impl_->clients) {
    if (!client.active) {
      continue;
    }

    ServerSnapshot networkSnapshot = snapshot;
    networkSnapshot.hasLocalClientState = true;
    networkSnapshot.localPlayerIndex = client.playerIndex;
    networkSnapshot.localSpectator =
      client.playerIndex == kNoAssignedPlayer;
    networkSnapshot.spectatorCount = spectatorCount;
    networkSnapshot.acknowledgedCommandDatagramSequence =
      client.latestCommandDatagramSequence;
    networkSnapshot.commandDatagramAckBits = client.commandDatagramAckBits;
    networkSnapshot.configurationRevision = impl_->configurationRevision;
    networkSnapshot.hasConfiguration =
      client.acknowledgedConfigurationRevision != impl_->configurationRevision;
    networkSnapshot.playerNameRevision = impl_->playerNameRevision;
    networkSnapshot.hasPlayerNames =
      client.acknowledgedPlayerNameRevision != impl_->playerNameRevision;
    for (std::size_t playerIndex = 0; playerIndex < kDuelPlayerCount; ++playerIndex) {
      if (networkSnapshot.localSpectator ||
          playerIndex != networkSnapshot.localPlayerIndex) {
        networkSnapshot.localHitFeedbackEvents[playerIndex].fill({});
        networkSnapshot.damageTakenEvents[playerIndex].clear();
      }
    }
    // Presentation-only aggregates stay out of the latency-sensitive gameplay
    // datagram and are sent independently to interested clients below.
    networkSnapshot.hasCombatStats = false;
    WirePacket wire;
    if (!encodeBoundedGameplaySnapshot(networkSnapshot, wire)) {
      impl_->error =
        "authoritative snapshot core exceeds 1200-byte UDP datagram ceiling";
      continue;
    }
    if (sendWire(impl_->socket, client.endpoint, wire)) {
      client.lastFullArenaRevision = snapshot.mapRevision;
      client.lastFullArenaTick = snapshot.serverTick;
    }

    if (client.wantsScoreboardStats &&
        (client.lastCombatStatsTick == 0 ||
         snapshot.serverTick - client.lastCombatStatsTick >=
           kCombatStatsRefreshTicks)) {
      bool sentAllStatsPages = true;
      for (std::size_t first = 0; first < kDuelPlayerCount;
           first += kCombatStatsPlayersPerPacket) {
        CombatStatsPacket stats;
        stats.serverTick = snapshot.serverTick;
        stats.firstPlayerIndex = static_cast<std::uint8_t>(first);
        stats.playerCount = static_cast<std::uint8_t>(std::min(
          kCombatStatsPlayersPerPacket,
          kDuelPlayerCount - first
        ));
        stats.round = snapshot.roundCombatStats;
        stats.match = snapshot.matchCombatStats;
        WirePacket statsWire;
        if (!encodeCombatStatsPacket(stats, statsWire) ||
            !sendWire(impl_->socket, client.endpoint, statsWire)) {
          sentAllStatsPages = false;
          break;
        }
      }
      if (sentAllStatsPages) {
        client.lastCombatStatsTick = snapshot.serverTick;
      }
    }

    if (impl_->chatHistory.messageCount == 0U) {
      continue;
    }
    const std::uint32_t latestSequence = impl_->chatHistory.messages[
      impl_->chatHistory.messageCount - 1U
    ].sequence;
    const auto now = Clock::now();
    const bool chatRetransmitThrottled =
      client.lastChatSend != Clock::time_point::min() &&
      now - client.lastChatSend < kChatRetransmitInterval;
    if (
      client.chatAckSequence == latestSequence ||
      chatRetransmitThrottled
    ) {
      continue;
    }

    std::size_t first = 0U;
    if (client.chatAckSequence != 0U) {
      while (
        first < impl_->chatHistory.messageCount &&
        impl_->chatHistory.messages[first].sequence != client.chatAckSequence
      ) {
        ++first;
      }
      if (first < impl_->chatHistory.messageCount) {
        ++first;
      } else {
        first = 0U;
      }
    }
    if (first >= impl_->chatHistory.messageCount) {
      continue;
    }
    ChatHistoryChunk chunk;
    chunk.oldestAvailableSequence = impl_->chatHistory.messages[0].sequence;
    chunk.latestSequence = latestSequence;
    chunk.messageCount = static_cast<std::uint8_t>(std::min(
      kChatHistoryChunkCapacity,
      static_cast<std::size_t>(impl_->chatHistory.messageCount) - first
    ));
    for (std::size_t index = 0; index < chunk.messageCount; ++index) {
      chunk.messages[index] = impl_->chatHistory.messages[first + index];
    }
    WirePacket chatWire;
    if (
      encodeChatHistoryChunk(chunk, chatWire) &&
      sendWire(impl_->socket, client.endpoint, chatWire)
    ) {
      client.lastChatSend = now;
    }
  }
}

void UdpServerTransport::publishChatHistory(const ChatHistory& history) {
  impl_->chatHistory = history;
}

void UdpServerTransport::sendProjectileUpdates(
  const ProjectileUpdatePacket& packet
) {
  WirePacket wire;
  if (!encodeProjectileUpdatePacket(packet, wire)) {
    impl_->error = "invalid or oversized projectile update packet";
    return;
  }
  for (const Impl::ClientSlot& client : impl_->clients) {
    if (client.active) {
      (void)sendWire(impl_->socket, client.endpoint, wire);
    }
  }
}

bool UdpServerTransport::receiveProjectileUpdates(ProjectileUpdatePacket&) {
  return false;
}

bool UdpServerTransport::receiveChatHistory(ChatHistoryChunk&) {
  return false;
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
  for (const Impl::ClientSlot& client : impl_->clients) {
    if (client.active && client.playerIndex < kDuelPlayerCount) {
      connected[client.playerIndex] = true;
    }
  }
  return connected;
}

std::array<std::uint32_t, kDuelPlayerCount>
UdpServerTransport::connectedPlayerSessions() const {
  std::array<std::uint32_t, kDuelPlayerCount> sessions = {};
  for (const Impl::ClientSlot& client : impl_->clients) {
    if (client.active && client.playerIndex < kDuelPlayerCount) {
      sessions[client.playerIndex] = client.session;
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

  void recordOutgoing(const WirePacket& wire) {
    telemetryOutgoingBytes += wire.size();
    PacketType type;
    if (inspectPacketType(wire, type) && type == PacketType::CommandBundle) {
      telemetry.lastCommandBytes = wire.size();
    }
  }

  void recordIncoming(const WirePacket& wire) {
    telemetryIncomingBytes += wire.size();
  }

  float incomingLossPercent() const {
    if (!hasSnapshotTick) return 0.0F;
    const std::uint32_t available = latestSnapshotTick - firstSnapshotTick + 1U;
    const std::size_t window = std::min<std::size_t>(
      snapshotTickTags.size(),
      static_cast<std::size_t>(available)
    );
    std::size_t received = 0;
    for (std::size_t offset = 0; offset < window; ++offset) {
      const std::uint32_t tick =
        latestSnapshotTick - static_cast<std::uint32_t>(offset);
      received += snapshotTickTags[tick % snapshotTickTags.size()] == tick ? 1U : 0U;
    }
    return window == 0 ? 0.0F :
      100.0F * static_cast<float>(window - received) /
        static_cast<float>(window);
  }

  void updateOutgoingLoss(const ServerSnapshot& snapshot) {
    const std::uint32_t latest = snapshot.acknowledgedCommandDatagramSequence;
    if (
      latest != 0 &&
      telemetry.acknowledgedCommandDatagramSequence != 0 &&
      latest != telemetry.acknowledgedCommandDatagramSequence &&
      !isSequenceNewer(
        latest,
        telemetry.acknowledgedCommandDatagramSequence
      )
    ) {
      // Reordered snapshots must not roll the transport acknowledgement window
      // backward and manufacture loss that the server already recovered.
      return;
    }
    telemetry.acknowledgedCommandDatagramSequence = latest;
    if (latest == 0) {
      telemetry.outgoingLossPercent = 0.0F;
      return;
    }
    const std::uint32_t previousCount = std::min(latest - 1U, 32U);
    const std::uint32_t validMask = previousCount == 32U
      ? 0xFFFFFFFFU
      : previousCount == 0U
        ? 0U
        : (1U << previousCount) - 1U;
    const std::uint32_t received =
      1U + std::popcount(snapshot.commandDatagramAckBits & validMask);
    const std::uint32_t expected = previousCount + 1U;
    telemetry.outgoingLossPercent =
      100.0F * static_cast<float>(expected - received) /
        static_cast<float>(expected);
  }

  void recordSnapshot(
    const ServerSnapshot& snapshot,
    std::size_t wireBytes,
    Clock::time_point now
  ) {
    telemetry.lastSnapshotBytes = wireBytes;
    ++telemetrySnapshotsReceived;
    updateOutgoingLoss(snapshot);

    const std::uint32_t tick = snapshot.serverTick;
    const bool alreadyReceived =
      hasSnapshotTick && snapshotTickTags[tick % snapshotTickTags.size()] == tick;
    bool acceptedNewestArrival = false;
    if (!hasSnapshotTick) {
      hasSnapshotTick = true;
      firstSnapshotTick = tick;
      latestSnapshotTick = tick;
      acceptedNewestArrival = true;
    } else if (isSequenceNewer(tick, latestSnapshotTick)) {
      const std::uint32_t tickDistance = tick - latestSnapshotTick;
      if (tickDistance > 1U) {
        telemetrySnapshotGaps += tickDistance - 1U;
      }
      if (hasSnapshotArrival) {
        const float actualMilliseconds =
          std::chrono::duration<float, std::milli>(now - lastSnapshotArrival).count();
        const float expectedMilliseconds =
          static_cast<float>(tickDistance) * kFixedTickSeconds * 1000.0F;
        const float variation = std::fabs(actualMilliseconds - expectedMilliseconds);
        snapshotJitterMilliseconds +=
          (variation - snapshotJitterMilliseconds) * (1.0F / 16.0F);
      }
      latestSnapshotTick = tick;
      acceptedNewestArrival = true;
    } else if (!alreadyReceived && tick != latestSnapshotTick) {
      ++telemetryLateSnapshots;
      ++telemetry.lateSnapshots;
      ++telemetry.reorderedSnapshots;
    }
    snapshotTickTags[tick % snapshotTickTags.size()] = tick;
    if (acceptedNewestArrival) {
      // Reordered/duplicate packets are diagnostics, not fresh controller
      // samples: they must not reset age or distort inter-arrival jitter.
      lastSnapshotArrival = now;
      hasSnapshotArrival = true;
    }
    telemetry.incomingLossPercent = incomingLossPercent();
  }

  void sampleTelemetry(Clock::time_point now) {
    if (lastTelemetrySample == Clock::time_point{}) {
      lastTelemetrySample = now;
      return;
    }
    const float elapsedSeconds =
      std::chrono::duration<float>(now - lastTelemetrySample).count();
    if (elapsedSeconds < 0.1F) return;

    NetworkTelemetrySample sample;
    sample.serial = ++telemetrySampleSerial;
    sample.pingMilliseconds = telemetry.pingMilliseconds;
    sample.snapshotJitterMilliseconds = snapshotJitterMilliseconds;
    sample.incomingLossPercent = incomingLossPercent();
    sample.outgoingLossPercent = telemetry.outgoingLossPercent;
    sample.incomingKilobitsPerSecond =
      static_cast<float>(telemetryIncomingBytes) * 8.0F /
      (elapsedSeconds * 1000.0F);
    sample.outgoingKilobitsPerSecond =
      static_cast<float>(telemetryOutgoingBytes) * 8.0F /
      (elapsedSeconds * 1000.0F);
    sample.snapshotAgeMilliseconds = hasSnapshotArrival
      ? std::chrono::duration<float, std::milli>(now - lastSnapshotArrival).count()
      : 0.0F;
    sample.snapshotsReceived = static_cast<std::uint16_t>(
      std::min<std::uint64_t>(telemetrySnapshotsReceived, 65535U)
    );
    sample.snapshotGaps = static_cast<std::uint16_t>(
      std::min<std::uint64_t>(telemetrySnapshotGaps, 65535U)
    );
    sample.lateSnapshots = static_cast<std::uint16_t>(
      std::min<std::uint64_t>(telemetryLateSnapshots, 65535U)
    );

    telemetry.valid = connected;
    telemetry.snapshotJitterMilliseconds = snapshotJitterMilliseconds;
    telemetry.incomingLossPercent = sample.incomingLossPercent;
    telemetry.incomingKilobitsPerSecond = sample.incomingKilobitsPerSecond;
    telemetry.outgoingKilobitsPerSecond = sample.outgoingKilobitsPerSecond;
    telemetry.snapshotRate =
      static_cast<float>(telemetrySnapshotsReceived) / elapsedSeconds;
    telemetry.snapshotAgeMilliseconds = sample.snapshotAgeMilliseconds;
    telemetry.history[telemetryHistoryNext] = sample;
    telemetryHistoryNext =
      (telemetryHistoryNext + 1U) % telemetry.history.size();
    telemetry.historyCount = std::min(
      telemetry.historyCount + 1U,
      telemetry.history.size()
    );

    telemetryIncomingBytes = 0;
    telemetryOutgoingBytes = 0;
    telemetrySnapshotsReceived = 0;
    telemetrySnapshotGaps = 0;
    telemetryLateSnapshots = 0;
    lastTelemetrySample = now;
  }

  void sendConnectedWire(const WirePacket& wire, Clock::time_point now) {
    if (!connected || !networkSim.active()) {
      if (sendWire(socket, server, wire)) recordOutgoing(wire);
      return;
    }
    if (
      networkSim.enqueue(ClientNetworkSimDirection::Outgoing, wire, now) ==
      ClientNetworkSimAction::Immediate
    ) {
      if (sendWire(socket, server, wire)) recordOutgoing(wire);
    }
  }

  void flushOutgoing(Clock::time_point now) {
    WirePacket wire;
    while (networkSim.popDue(ClientNetworkSimDirection::Outgoing, now, wire)) {
      if (sendWire(socket, server, wire)) recordOutgoing(wire);
    }
  }

  bool acceptProjectileGeneration(
    std::uint32_t mapRevision,
    std::uint32_t projectileRevision
  ) {
    if (mapRevision == 0U || projectileRevision == 0U) {
      return false;
    }
    if (latestProjectileMapRevision == 0U) {
      latestProjectileMapRevision = mapRevision;
      latestProjectileRevision = projectileRevision;
      return true;
    }
    if (mapRevision != latestProjectileMapRevision) {
      if (!isSequenceNewer(mapRevision, latestProjectileMapRevision)) {
        return false;
      }
      projectileUpdates.clear();
      latestProjectileMapRevision = mapRevision;
      latestProjectileRevision = projectileRevision;
      return true;
    }
    if (projectileRevision == latestProjectileRevision) {
      return true;
    }
    if (!isSequenceNewer(projectileRevision, latestProjectileRevision)) {
      return false;
    }
    projectileUpdates.clear();
    latestProjectileRevision = projectileRevision;
    return true;
  }

  void observeSnapshotProjectileGeneration(const ServerSnapshot& snapshot) {
    if (latestProjectileMapRevision == 0U ||
        isSequenceNewer(
          snapshot.mapRevision,
          latestProjectileMapRevision
        )) {
      projectileUpdates.clear();
      latestProjectileMapRevision = snapshot.mapRevision;
      latestProjectileRevision = snapshot.projectileRevision;
      return;
    }
    if (snapshot.mapRevision == latestProjectileMapRevision &&
        snapshot.projectileRevision != latestProjectileRevision &&
        isSequenceNewer(
          snapshot.projectileRevision,
          latestProjectileRevision
        )) {
      projectileUpdates.clear();
      latestProjectileRevision = snapshot.projectileRevision;
    }
  }

  void dispatchWire(const WirePacket& wire, Clock::time_point now) {
    PacketType type;
    if (!inspectPacketType(wire, type)) {
      return;
    }
    recordIncoming(wire);
    if (type == PacketType::Snapshot && connected) {
      auto snapshot = std::make_unique<ServerSnapshot>();
      const auto decodeStart = Clock::now();
      if (decodeServerSnapshot(wire, *snapshot)) {
        observeSnapshotProjectileGeneration(*snapshot);
        const auto decodeEnd = Clock::now();
        ++snapshotDiagnostics.snapshotPacketsDecoded;
        snapshotDiagnostics.snapshotDecodeMilliseconds =
          std::chrono::duration<float, std::milli>(
            decodeEnd - decodeStart
          ).count();
        // Queue validated wire rather than retaining the large decoded object;
        // ClientGame performs the state-producing decode when it consumes it.
        snapshots.push_back(wire);
        recordSnapshot(*snapshot, wire.size(), now);
        snapshotDiagnostics.snapshotQueueDepth = snapshots.size();
        lastServerPacket = now;
      }
    } else if (type == PacketType::ProjectileUpdates && connected) {
      ProjectileUpdatePacket packet;
      if (
        decodeProjectileUpdatePacket(wire, packet) &&
        acceptProjectileGeneration(
          packet.mapRevision,
          packet.projectileRevision
        )
      ) {
        if (
          projectileUpdates.size() ==
          kMaxQueuedProjectileUpdatePackets
        ) {
          projectileUpdates.pop_front();
        }
        projectileUpdates.push_back(wire);
        lastServerPacket = now;
      }
    } else if (type == PacketType::ChatHistory && connected) {
      ChatHistoryChunk chunk;
      if (decodeChatHistoryChunk(wire, chunk)) {
        chatHistory.push_back(wire);
        const ChatMessage& last = chunk.messages[chunk.messageCount - 1U];
        WirePacket ackWire;
        if (encodeChatHistoryAck(ChatHistoryAck{last.sequence}, ackWire)) {
          sendConnectedWire(ackWire, now);
        }
        lastServerPacket = now;
      }
    } else if (type == PacketType::CombatStats && connected) {
      CombatStatsPacket stats;
      if (decodeCombatStatsPacket(wire, stats) &&
          (pendingCombatStatsServerTick == 0U ||
           stats.serverTick == pendingCombatStatsServerTick ||
           isSequenceNewer(stats.serverTick, pendingCombatStatsServerTick))) {
        if (stats.serverTick != pendingCombatStatsServerTick) {
          pendingCombatStatsServerTick = stats.serverTick;
          pendingCombatStatsPlayerMask = 0U;
          pendingRoundCombatStats = {};
          pendingMatchCombatStats = {};
        }
        const std::size_t first = stats.firstPlayerIndex;
        const std::size_t count = stats.playerCount;
        for (std::size_t index = first; index < first + count; ++index) {
          pendingRoundCombatStats[index] = stats.round[index];
          pendingMatchCombatStats[index] = stats.match[index];
          pendingCombatStatsPlayerMask |= std::uint32_t{1} << index;
        }
        constexpr std::uint32_t kAllCombatStatsPlayers =
          (std::uint32_t{1} << kDuelPlayerCount) - 1U;
        if (pendingCombatStatsPlayerMask == kAllCombatStatsPlayers) {
          roundCombatStats = pendingRoundCombatStats;
          matchCombatStats = pendingMatchCombatStats;
          combatStatsServerTick = pendingCombatStatsServerTick;
          hasCombatStats = true;
        }
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
        if (telemetry.pingMilliseconds == 0.0F) {
          telemetry.pingMilliseconds = pingMs;
        } else {
          const float difference = std::fabs(pingMs - telemetry.pingMilliseconds);
          telemetry.pingVariationMilliseconds +=
            (difference - telemetry.pingVariationMilliseconds) * 0.25F;
          telemetry.pingMilliseconds +=
            (pingMs - telemetry.pingMilliseconds) * 0.125F;
        }
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
          // The echoed nonce prevents delayed accepts from a previous connection
          // attempt from assigning this client to a stale server-side session.
          connected = true;
          timedOut = false;
          assignedClient = accept.clientIndex;
          assignedPlayer = accept.playerIndex;
          snapshots.clear();
          projectileUpdates.clear();
          chatHistory.clear();
          latestProjectileMapRevision = 0U;
          latestProjectileRevision = 0U;
          configurationRevision = 0;
          playerNameRevision = 0;
          playerNameCache = {};
          hasCombatStats = false;
          combatStatsServerTick = 0;
          pendingCombatStatsServerTick = 0;
          pendingCombatStatsPlayerMask = 0;
          roundCombatStats = {};
          matchCombatStats = {};
          pendingRoundCombatStats = {};
          pendingMatchCombatStats = {};
          telemetry = {};
          snapshotTickTags = {};
          telemetryHistoryNext = 0;
          telemetrySampleSerial = 0;
          telemetryIncomingBytes = 0;
          telemetryOutgoingBytes = 0;
          telemetrySnapshotsReceived = 0;
          telemetrySnapshotGaps = 0;
          telemetryLateSnapshots = 0;
          hasSnapshotTick = false;
          hasSnapshotArrival = false;
          snapshotJitterMilliseconds = 0.0F;
          lastTelemetrySample = Clock::time_point{};
          nextCommandDatagramSequence = 1;
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
      snapshots.clear();
      projectileUpdates.clear();
      chatHistory.clear();
      latestProjectileMapRevision = 0U;
      latestProjectileRevision = 0U;
      configurationRevision = 0;
      playerNameRevision = 0;
      playerNameCache = {};
      hasCombatStats = false;
      combatStatsServerTick = 0;
      pendingCombatStatsServerTick = 0;
      pendingCombatStatsPlayerMask = 0;
      roundCombatStats = {};
      matchCombatStats = {};
      pendingRoundCombatStats = {};
      pendingMatchCombatStats = {};
      networkSim.clear();
    }
    sampleTelemetry(Clock::now());
  }

  SocketRuntime runtime;
  std::string host;
  std::uint16_t port = 0;
  SocketHandle socket = kInvalidSocket;
  Endpoint server = {};
  std::deque<WirePacket> snapshots;
  std::deque<WirePacket> projectileUpdates;
  std::deque<WirePacket> chatHistory;
  SnapshotDiagnostics snapshotDiagnostics = {};
  NetworkTelemetry telemetry = {};
  std::array<std::uint32_t, 256> snapshotTickTags = {};
  std::size_t telemetryHistoryNext = 0;
  std::uint64_t telemetrySampleSerial = 0;
  std::uint64_t telemetryIncomingBytes = 0;
  std::uint64_t telemetryOutgoingBytes = 0;
  std::uint64_t telemetrySnapshotsReceived = 0;
  std::uint64_t telemetrySnapshotGaps = 0;
  std::uint64_t telemetryLateSnapshots = 0;
  std::uint32_t firstSnapshotTick = 0;
  std::uint32_t latestSnapshotTick = 0;
  std::uint32_t latestProjectileMapRevision = 0;
  std::uint32_t latestProjectileRevision = 0;
  bool hasSnapshotTick = false;
  bool hasSnapshotArrival = false;
  float snapshotJitterMilliseconds = 0.0F;
  Clock::time_point lastSnapshotArrival = {};
  Clock::time_point lastTelemetrySample = {};
  std::deque<CommandPacket> commandHistory;
  ServerSnapshot configurationCache = {};
  std::uint32_t configurationRevision = 0;
  ServerSnapshot playerNameCache = {};
  std::uint32_t playerNameRevision = 0;
  std::array<RoundCombatStats, kDuelPlayerCount> roundCombatStats = {};
  std::array<RoundCombatStats, kDuelPlayerCount> matchCombatStats = {};
  std::array<RoundCombatStats, kDuelPlayerCount> pendingRoundCombatStats = {};
  std::array<RoundCombatStats, kDuelPlayerCount> pendingMatchCombatStats = {};
  std::uint32_t combatStatsServerTick = 0;
  std::uint32_t pendingCombatStatsServerTick = 0;
  std::uint32_t pendingCombatStatsPlayerMask = 0;
  bool hasCombatStats = false;
  ClientNetworkSimulator networkSim;
  std::uint32_t nonce = 0;
  std::uint32_t pingToken = 0;
  std::uint32_t nextCommandDatagramSequence = 1;
  std::uint8_t assignedPlayer = 0;
  std::uint8_t assignedClient = 0;
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
  impl_->projectileUpdates.clear();
  impl_->latestProjectileMapRevision = 0U;
  impl_->latestProjectileRevision = 0U;
  impl_->configurationRevision = 0;
  impl_->playerNameRevision = 0;
  impl_->playerNameCache = {};
  impl_->hasCombatStats = false;
  impl_->combatStatsServerTick = 0;
  impl_->pendingCombatStatsServerTick = 0;
  impl_->pendingCombatStatsPlayerMask = 0;
  impl_->roundCombatStats = {};
  impl_->matchCombatStats = {};
  impl_->pendingRoundCombatStats = {};
  impl_->pendingMatchCombatStats = {};
  impl_->chatHistory.clear();
  impl_->telemetry.valid = false;
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
  stampedPacket.clientIndex = impl_->assignedClient;
  // The server owns the mapping and overwrites this after authentication. The
  // repeated value only keeps compact command histories internally coherent.
  stampedPacket.playerIndex = impl_->assignedPlayer;
  stampedPacket.clientNonce = impl_->nonce;
  stampedPacket.acknowledgedConfigurationRevision =
    impl_->configurationRevision;
  stampedPacket.acknowledgedPlayerNameRevision =
    impl_->playerNameRevision;

  WirePacket singleCommandWire;
  if (!encodeCommandPacket(stampedPacket, singleCommandWire)) {
    impl_->commandHistory.clear();
    return;
  }

  impl_->commandHistory.push_back(stampedPacket);
  // Resend a short history in every UDP bundle. Server-side wrap-safe sequence
  // filtering makes duplicates harmless while recovering isolated packet loss.
  while (impl_->commandHistory.size() > kMaxBundledCommands) {
    impl_->commandHistory.pop_front();
  }

  CommandBundle bundle;
  bundle.datagramSequence = impl_->nextCommandDatagramSequence++;
  if (impl_->nextCommandDatagramSequence == 0) {
    impl_->nextCommandDatagramSequence = 1;
  }
  bundle.actionEdges = stampedPacket.actionEdges;
  WirePacket wire;
  // Prefer the newest inputs if an unusual control payload makes the complete
  // history exceed the conservative no-fragmentation datagram budget.
  for (std::size_t first = 0; first < impl_->commandHistory.size(); ++first) {
    bundle.commandCount = static_cast<std::uint8_t>(
      impl_->commandHistory.size() - first
    );
    for (std::size_t index = 0; index < bundle.commandCount; ++index) {
      bundle.commands[index] = impl_->commandHistory[first + index];
    }
    if (
      encodeCommandBundle(bundle, wire) &&
      wire.size() <= kMaxCommandDatagramBytes
    ) {
      impl_->sendConnectedWire(wire, Clock::now());
      return;
    }
  }

  // If accumulated redundancy no longer fits, preserve current input latency by
  // falling back to the already validated single command and restart history.
  impl_->commandHistory.clear();
  impl_->commandHistory.push_back(stampedPacket);
  impl_->sendConnectedWire(singleCommandWire, Clock::now());
}

bool UdpClientTransport::receiveCommand(CommandPacket&) {
  return false;
}

void UdpClientTransport::sendSnapshot(const ServerSnapshot&) {}

void UdpClientTransport::sendProjectileUpdates(
  const ProjectileUpdatePacket&
) {}

bool UdpClientTransport::receiveProjectileUpdates(
  ProjectileUpdatePacket& packet
) {
  impl_->pump();
  if (impl_->projectileUpdates.empty()) {
    return false;
  }
  const WirePacket wire = std::move(impl_->projectileUpdates.front());
  impl_->projectileUpdates.pop_front();
  return decodeProjectileUpdatePacket(wire, packet);
}

bool UdpClientTransport::receiveSnapshot(ServerSnapshot& snapshot) {
  impl_->pump();
  if (impl_->snapshots.empty()) {
    impl_->snapshotDiagnostics.snapshotQueueDepth = 0;
    return false;
  }
  const WirePacket wire = impl_->snapshots.front();
  impl_->snapshots.pop_front();
  impl_->snapshotDiagnostics.snapshotQueueDepth = impl_->snapshots.size();
  if (!decodeServerSnapshot(wire, snapshot)) {
    return false;
  }
  if (snapshot.hasLocalClientState) {
    impl_->assignedPlayer = snapshot.localSpectator
      ? kNoAssignedPlayer : snapshot.localPlayerIndex;
  }
  if (snapshot.hasConfiguration) {
    if (impl_->configurationRevision == 0 ||
        snapshot.configurationRevision == impl_->configurationRevision ||
        isSequenceNewer(
          snapshot.configurationRevision,
          impl_->configurationRevision
        )) {
      copySnapshotConfiguration(impl_->configurationCache, snapshot);
      impl_->configurationRevision = snapshot.configurationRevision;
    } else {
      // A reordered snapshot may carry an older full block; retain the newest
      // acknowledged configuration while still consuming its gameplay state.
      copySnapshotConfiguration(snapshot, impl_->configurationCache);
    }
  } else {
    if (impl_->configurationRevision == 0 ||
        isSequenceNewer(
          snapshot.configurationRevision,
          impl_->configurationRevision
        )) {
      return false;
    }
    copySnapshotConfiguration(snapshot, impl_->configurationCache);
    snapshot.hasConfiguration = true;
  }
  if (snapshot.hasPlayerNames) {
    if (impl_->playerNameRevision == 0U ||
        snapshot.playerNameRevision == impl_->playerNameRevision ||
        isSequenceNewer(snapshot.playerNameRevision, impl_->playerNameRevision)) {
      copySnapshotPlayerNames(impl_->playerNameCache, snapshot);
      impl_->playerNameRevision = snapshot.playerNameRevision;
    } else {
      copySnapshotPlayerNames(snapshot, impl_->playerNameCache);
      snapshot.playerNameRevision = impl_->playerNameRevision;
    }
  } else {
    if (impl_->playerNameRevision == 0U ||
        isSequenceNewer(snapshot.playerNameRevision, impl_->playerNameRevision)) {
      return false;
    }
    copySnapshotPlayerNames(snapshot, impl_->playerNameCache);
    snapshot.playerNameRevision = impl_->playerNameRevision;
    snapshot.hasPlayerNames = true;
  }
  if (snapshot.hasCombatStats) {
    impl_->roundCombatStats = snapshot.roundCombatStats;
    impl_->matchCombatStats = snapshot.matchCombatStats;
    impl_->hasCombatStats = true;
  } else if (impl_->hasCombatStats) {
    // Scoreboard aggregates update at a lower rate but remain stable to consumers.
    snapshot.roundCombatStats = impl_->roundCombatStats;
    snapshot.matchCombatStats = impl_->matchCombatStats;
    snapshot.hasCombatStats = true;
  }
  return true;
}

void UdpClientTransport::publishChatHistory(const ChatHistory&) {}

bool UdpClientTransport::receiveChatHistory(ChatHistoryChunk& chunk) {
  impl_->pump();
  if (impl_->chatHistory.empty()) {
    return false;
  }
  const WirePacket wire = std::move(impl_->chatHistory.front());
  impl_->chatHistory.pop_front();
  return decodeChatHistoryChunk(wire, chunk);
}

SnapshotDiagnostics UdpClientTransport::snapshotDiagnostics() const {
  SnapshotDiagnostics diagnostics = impl_->snapshotDiagnostics;
  diagnostics.snapshotQueueDepth = impl_->snapshots.size();
  return diagnostics;
}

NetworkTelemetry UdpClientTransport::networkTelemetry() const {
  NetworkTelemetry result = impl_->telemetry;
  result.valid = impl_->connected;
  if (impl_->hasSnapshotArrival) {
    result.snapshotAgeMilliseconds = std::chrono::duration<float, std::milli>(
      Clock::now() - impl_->lastSnapshotArrival
    ).count();
  }
  const std::size_t count = impl_->telemetry.historyCount;
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t source =
      (impl_->telemetryHistoryNext + impl_->telemetry.history.size() - count + index) %
      impl_->telemetry.history.size();
    result.history[index] = impl_->telemetry.history[source];
  }
  result.historyCount = count;
  return result;
}

bool UdpClientTransport::connected() const {
  return impl_->connected;
}

bool UdpClientTransport::timedOut() const {
  return impl_->timedOut;
}

std::uint8_t UdpClientTransport::clientIndex() const {
  return impl_->assignedClient;
}

std::uint8_t UdpClientTransport::playerIndex() const {
  return impl_->assignedPlayer;
}

bool UdpClientTransport::spectator() const {
  return impl_->assignedPlayer == kNoAssignedPlayer;
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

const std::deque<ClientNetworkSimulationDecision>&
UdpClientTransport::networkSimulationDecisions() const {
  return impl_->networkSim.decisions();
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
