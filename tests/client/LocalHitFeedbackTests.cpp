#include "client/LocalHitFeedback.hpp"

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

} // namespace

int main() {
  int failures = 0;

  std::array<lg::LocalHitFeedbackEvent, lg::kLocalHitFeedbackEventWindow>
    events = {};
  events[0].active = true;
  events[0].sequence = 1;
  events[0].targetPlayerIndex = 1;
  events[0].damageApplied = 80;
  events[0].headshot = true;
  events[0].weapon = lg::Weapon::Railgun;
  events[1].active = true;
  events[1].sequence = 2;
  events[1].targetPlayerIndex = 2;
  events[1].damageApplied = 6;
  events[1].weapon = lg::Weapon::LightningGun;

  lg::LocalHitFeedbackDedupeState state;
  const lg::LocalHitFeedbackBatch first =
    lg::consumeLocalHitFeedbackEvents(events, state);
  failures += expect(first.active, "first feedback snapshot should be consumed");
  failures += expect(
    first.hitTargets[1] && first.hitTargets[2],
    "feedback batch should preserve exact damaged targets"
  );
  failures += expect(
    first.damageByTarget[1] == 80 && first.damageByTarget[2] == 6,
    "feedback batch should preserve authoritative damage by target"
  );
  failures += expect(
    first.headshotHit &&
      first.headshotTargets[1] &&
      !first.headshotTargets[2],
    "feedback batch should preserve authoritative headshot targets"
  );
  failures += expect(
    first.lightningGunHit,
    "feedback batch should report whether an LG hit occurred"
  );

  const lg::LocalHitFeedbackBatch repeated =
    lg::consumeLocalHitFeedbackEvents(events, state);
  failures += expect(
    !repeated.active,
    "same sequence in a repeated snapshot should be deduped"
  );

  std::array<lg::LocalHitFeedbackEvent, lg::kLocalHitFeedbackEventWindow>
    otherAttackerEvents = {};
  otherAttackerEvents[0].active = true;
  otherAttackerEvents[0].sequence = 1;
  otherAttackerEvents[0].targetPlayerIndex = 0;
  otherAttackerEvents[0].weapon = lg::Weapon::MachineGun;
  lg::LocalHitFeedbackDedupeState otherAttackerState;
  const lg::LocalHitFeedbackBatch otherAttacker =
    lg::consumeLocalHitFeedbackEvents(otherAttackerEvents, otherAttackerState);
  failures += expect(
    otherAttacker.active && otherAttacker.hitTargets[0],
    "another attacker's event window should be independently consumable"
  );
  failures += expect(
    !lg::consumeLocalHitFeedbackEvents(otherAttackerEvents, otherAttackerState)
       .active,
    "another attacker's repeated sequence should also dedupe"
  );

  lg::LocalHitFeedbackDedupeState delayedState;
  std::array<lg::LocalHitFeedbackEvent, lg::kLocalHitFeedbackEventWindow>
    delayedWindow = {};
  for (std::uint32_t index = 0; index < lg::kLocalHitFeedbackEventWindow; ++index) {
    delayedWindow[index].active = true;
    delayedWindow[index].sequence = index + 3;
    delayedWindow[index].targetPlayerIndex = 1;
    delayedWindow[index].weapon = lg::Weapon::MachineGun;
  }
  const lg::LocalHitFeedbackBatch delayed =
    lg::consumeLocalHitFeedbackEvents(delayedWindow, delayedState);
  failures += expect(
    delayed.active &&
      delayed.hitTargets[1] &&
      delayedState.lastSequence == 6,
    "latest feedback should be consumed even when earlier events rolled out of the window"
  );

  std::array<lg::LocalHitFeedbackEvent, lg::kLocalHitFeedbackEventWindow>
    oldWindow = {};
  oldWindow[0].active = true;
  oldWindow[0].sequence = 2;
  oldWindow[0].targetPlayerIndex = 1;
  oldWindow[0].weapon = lg::Weapon::MachineGun;
  failures += expect(
    !lg::consumeLocalHitFeedbackEvents(oldWindow, delayedState).active,
    "an older snapshot after a newer one should not reactivate feedback"
  );

  std::array<lg::LocalHitFeedbackEvent, lg::kLocalHitFeedbackEventWindow>
    refreshedWindow = {};
  refreshedWindow[0].active = true;
  refreshedWindow[0].sequence = 9;
  refreshedWindow[0].targetPlayerIndex = 1;
  refreshedWindow[0].weapon = lg::Weapon::LightningGun;
  const lg::LocalHitFeedbackBatch refreshed =
    lg::consumeLocalHitFeedbackEvents(refreshedWindow, delayedState);
  failures += expect(
    refreshed.active && refreshed.lightningGunHit,
    "a later sustained LG damage event should refresh feedback"
  );
  failures += expect(
    !lg::consumeLocalHitFeedbackEvents(refreshedWindow, delayedState).active,
    "a duplicated sustained LG snapshot should not refresh feedback twice"
  );

  lg::LocalHitFeedbackDedupeState wrapState;
  wrapState.hasLastSequence = true;
  wrapState.lastSequence = 0xFFFF'FFFEU;
  std::array<lg::LocalHitFeedbackEvent, lg::kLocalHitFeedbackEventWindow>
    wrapWindow = {};
  wrapWindow[0].active = true;
  wrapWindow[0].sequence = 1;
  wrapWindow[0].targetPlayerIndex = 1;
  wrapWindow[0].weapon = lg::Weapon::Railgun;
  failures += expect(
    lg::consumeLocalHitFeedbackEvents(wrapWindow, wrapState).active &&
      wrapState.lastSequence == 1,
    "sequence wraparound should still allow newer feedback"
  );

  return failures == 0 ? 0 : 1;
}
