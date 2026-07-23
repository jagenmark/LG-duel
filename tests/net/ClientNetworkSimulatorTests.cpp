#include "net/ClientNetworkSimulator.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::WirePacket packet(std::uint8_t value) {
  return lg::WirePacket{value};
}

} // namespace

int main() {
  int failures = 0;
  lg::ClientNetworkSimulator simulator;
  const auto now = lg::ClientNetworkSimulator::Clock::time_point{};

  {
    lg::WirePacket wire = packet(1);
    failures += expect(
      simulator.enqueue(lg::ClientNetworkSimDirection::Outgoing, wire, now) ==
        lg::ClientNetworkSimAction::Immediate,
      "zero cvars should use the immediate fast path"
    );
    failures += expect(
      simulator.stats().queuedOutgoingPackets == 0,
      "zero cvars should not queue outgoing packets"
    );
  }

  {
    simulator.clear();
    simulator.setConfig({50, 0, 0, 0, 123});
    failures += expect(
      simulator.enqueue(lg::ClientNetworkSimDirection::Incoming, packet(2), now) ==
        lg::ClientNetworkSimAction::Queued,
      "latency should queue incoming packets"
    );
    lg::WirePacket delivered;
    failures += expect(
      !simulator.popDue(
        lg::ClientNetworkSimDirection::Incoming,
        now + std::chrono::milliseconds(49),
        delivered
      ),
      "latency should not deliver before its deadline"
    );
    failures += expect(
      simulator.popDue(
        lg::ClientNetworkSimDirection::Incoming,
        now + std::chrono::milliseconds(50),
        delivered
      ) && delivered == packet(2),
      "latency should deliver at its deadline"
    );
    failures += expect(
      simulator.decisions().size() == 1U &&
        simulator.decisions().front().direction ==
          lg::ClientNetworkSimDirection::Incoming &&
        simulator.decisions().front().action ==
          lg::ClientNetworkSimAction::Queued &&
        simulator.decisions().front().delayMs == 50,
      "latency decisions should be recorded with direction and delay"
    );
  }

  {
    simulator.clear();
    simulator.setConfig({20, 10, 0, 0, 456});
    for (std::uint8_t value = 0; value < 24; ++value) {
      (void)simulator.enqueue(lg::ClientNetworkSimDirection::Outgoing, packet(value), now);
    }
    lg::WirePacket delivered;
    failures += expect(
      !simulator.popDue(
        lg::ClientNetworkSimDirection::Outgoing,
        now + std::chrono::milliseconds(9),
        delivered
      ),
      "jitter should never make delay negative or below latency minus jitter"
    );
    int deliveredBy20 = 0;
    while (simulator.popDue(
      lg::ClientNetworkSimDirection::Outgoing,
      now + std::chrono::milliseconds(20),
      delivered
    )) {
      ++deliveredBy20;
    }
    failures += expect(
      deliveredBy20 > 0 && deliveredBy20 < 24,
      "jitter should vary delivery times around the base latency"
    );
  }

  {
    simulator.clear();
    simulator.setConfig({0, 0, 100, 0, 789});
    failures += expect(
      simulator.enqueue(lg::ClientNetworkSimDirection::Outgoing, packet(3), now) ==
        lg::ClientNetworkSimAction::Dropped,
      "100 percent loss should drop outgoing packets"
    );
    lg::WirePacket delivered;
    failures += expect(
      !simulator.popDue(lg::ClientNetworkSimDirection::Outgoing, now, delivered),
      "dropped outgoing packets should never be delivered"
    );
    failures += expect(
      simulator.decisions().size() == 1U &&
        simulator.decisions().front().action ==
          lg::ClientNetworkSimAction::Dropped,
      "loss decisions should be recorded"
    );
  }

  {
    simulator.clear();
    simulator.setConfig({0, 0, 0, 100, 321});
    (void)simulator.enqueue(lg::ClientNetworkSimDirection::Incoming, packet(4), now);
    (void)simulator.enqueue(lg::ClientNetworkSimDirection::Incoming, packet(5), now);
    lg::WirePacket delivered;
    failures += expect(
      simulator.popDue(
        lg::ClientNetworkSimDirection::Incoming,
        now + std::chrono::milliseconds(16),
        delivered
      ) && delivered == packet(5),
      "deterministic reordering should allow a later packet to arrive first"
    );
    failures += expect(
      simulator.popDue(
        lg::ClientNetworkSimDirection::Incoming,
        now + std::chrono::milliseconds(17),
        delivered
      ) && delivered == packet(4),
      "reordered earlier packet should still be delivered intact"
    );
    failures += expect(
      simulator.decisions().size() == 2U &&
        simulator.decisions().front().reordered &&
        simulator.decisions().back().reordered,
      "reorder decisions should be recorded for each chosen datagram"
    );
  }

  {
    simulator.clear();
    failures += expect(
      simulator.decisions().empty(),
      "session clear should discard prior decision evidence"
    );
    simulator.setConfig({10, 0, 0, 0, 654});
    (void)simulator.enqueue(lg::ClientNetworkSimDirection::Outgoing, packet(6), now);
    (void)simulator.enqueue(lg::ClientNetworkSimDirection::Outgoing, packet(7), now);
    lg::WirePacket delivered;
    failures += expect(
      simulator.popDue(
        lg::ClientNetworkSimDirection::Outgoing,
        now + std::chrono::milliseconds(10),
        delivered
      ) && delivered == packet(6),
      "same deliverAt should preserve insertion order for the first packet"
    );
    failures += expect(
      simulator.popDue(
        lg::ClientNetworkSimDirection::Outgoing,
        now + std::chrono::milliseconds(10),
        delivered
      ) && delivered == packet(7),
      "same deliverAt should preserve insertion order for the second packet"
    );
  }

  {
    simulator.clear();
    simulator.setConfig({50, 0, 0, 0, 987});
    (void)simulator.enqueue(lg::ClientNetworkSimDirection::Incoming, packet(8), now);
    simulator.setConfig({0, 0, 0, 0, 987});
    failures += expect(
      simulator.enqueue(lg::ClientNetworkSimDirection::Incoming, packet(9), now) ==
        lg::ClientNetworkSimAction::Immediate,
      "new packets should go direct after cvars are set to zero"
    );
    lg::WirePacket delivered;
    failures += expect(
      !simulator.popDue(
        lg::ClientNetworkSimDirection::Incoming,
        now + std::chrono::milliseconds(49),
        delivered
      ),
      "already queued packets should keep their original delivery schedule"
    );
    failures += expect(
      simulator.popDue(
        lg::ClientNetworkSimDirection::Incoming,
        now + std::chrono::milliseconds(50),
        delivered
      ) && delivered == packet(8),
      "already queued packets should deliver at their original deadline"
    );
  }

  return failures == 0 ? 0 : 1;
}
