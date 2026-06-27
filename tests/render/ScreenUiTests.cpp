#include "app/TextInput.hpp"
#include "render/ScreenUi.hpp"
#include "render/ChatLayout.hpp"
#include "render/ConsoleLayout.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <iostream>
#include <string_view>
#include <variant>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

const lg::Text2D* findText(
  const lg::DrawList2D& drawList,
  std::string_view value
) {
  for (const lg::DrawCommand2D& command : drawList.overlayCommands) {
    if (const auto* text = std::get_if<lg::Text2D>(&command)) {
      if (text->text == value) {
        return text;
      }
    }
  }
  return nullptr;
}

bool commandTouchesRightHud(const lg::DrawCommand2D& command) {
  if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
    for (const lg::ScreenPoint& point : quad->points) {
      if (point.x > 1180.0F) {
        return true;
      }
    }
  }
  if (const auto* line = std::get_if<lg::Line2D>(&command)) {
    return line->start.x > 1180.0F || line->end.x > 1180.0F;
  }
  return false;
}

bool commandOverlapsRect(
  const lg::DrawCommand2D& command,
  float x,
  float y,
  float width,
  float height
) {
  const float right = x + width;
  const float bottom = y + height;
  if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
    float minX = quad->points[0].x;
    float maxX = quad->points[0].x;
    float minY = quad->points[0].y;
    float maxY = quad->points[0].y;
    for (const lg::ScreenPoint& point : quad->points) {
      minX = std::min(minX, point.x);
      maxX = std::max(maxX, point.x);
      minY = std::min(minY, point.y);
      maxY = std::max(maxY, point.y);
    }
    const float commandWidth = maxX - minX;
    const float commandHeight = maxY - minY;
    return commandWidth <= 50.0F && commandHeight <= 45.0F &&
      maxX > x && minX < right && maxY > y && minY < bottom;
  }
  if (const auto* line = std::get_if<lg::Line2D>(&command)) {
    const float halfWidth = line->width * 0.5F;
    const float minX = std::min(line->start.x, line->end.x) - halfWidth;
    const float maxX = std::max(line->start.x, line->end.x) + halfWidth;
    const float minY = std::min(line->start.y, line->end.y) - halfWidth;
    const float maxY = std::max(line->start.y, line->end.y) + halfWidth;
    const float commandWidth = maxX - minX;
    const float commandHeight = maxY - minY;
    return commandWidth <= 50.0F && commandHeight <= 45.0F &&
      maxX > x && minX < right && maxY > y && minY < bottom;
  }
  return false;
}

} // namespace

int main() {
  int failures = 0;

  lg::PlayerState opponent;
  opponent.health = 50;
  lg::RenderSettings settings;
  lg::HudRenderState hud;
  hud.selectedWeapon = lg::Weapon::LightningGun;
  hud.showOpponentHealthBar = true;
  hud.topLeftLines = {"FPS 240"};
  hud.bottomCenterLines = {"SPEED 320 UPS", "HEALTH 100"};
  hud.scoreboardOpen = true;
  hud.scoreboardLines = {"SCOREBOARD", "PLAYER  SCORE"};
  lg::ConsoleRenderState console;

  {
    lg::LightningGunResult beam;
    beam.active = true;
    settings.beamAlpha = 0.5F;
    settings.beamHitAmount = 1.0F;
    settings.beamPulse = 1.0F;
    const lg::DrawList2D overlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      beam,
      lg::Weapon::LightningGun,
      lg::Weapon::LightningGun,
      1.0F,
      settings
    );
    const auto* line = overlay.overlayCommands.empty()
      ? nullptr
      : std::get_if<lg::Line2D>(&overlay.overlayCommands.front());
    failures += expect(
      line != nullptr &&
        line->start.y > 720.0F &&
        line->end.x == 640.0F &&
        line->end.y == 360.0F &&
        line->color.alpha == 127 &&
        line->width > settings.beamWidth,
      "perspective local beam should pulse without moving its endpoints"
    );
    failures += expect(
      overlay.overlayCommands.size() >= 8,
      "perspective overlay should include a simple lightning gun viewmodel"
    );
  }

  {
    const lg::DrawList2D idleOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::LightningGun,
      lg::Weapon::LightningGun,
      1.0F,
      settings
    );
    failures += expect(
      !idleOverlay.overlayCommands.empty() &&
        std::get_if<lg::FilledQuad2D>(
          &idleOverlay.overlayCommands.front()
        ) != nullptr,
      "lightning gun viewmodel should remain visible while idle"
    );
  }

  {
    const lg::DrawList2D railOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::Railgun,
      lg::Weapon::LightningGun,
      1.0F,
      settings
    );
    bool foundRailCore = false;
    for (const lg::DrawCommand2D& command : railOverlay.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundRailCore =
          foundRailCore ||
          (
            quad->color.red == 90 &&
            quad->color.green == 220 &&
            quad->color.blue == 255
          );
      }
    }
    failures += expect(foundRailCore, "railgun viewmodel should use its own rail core");
  }

  {
    const lg::DrawList2D rocketOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::RocketLauncher,
      lg::Weapon::LightningGun,
      1.0F,
      settings
    );
    bool foundRocketAccent = false;
    for (const lg::DrawCommand2D& command : rocketOverlay.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundRocketAccent =
          foundRocketAccent ||
          (quad->color.red == 185 && quad->color.green == 120);
      }
    }
    failures += expect(
      foundRocketAccent,
      "rocket launcher viewmodel should use its own accent"
    );
  }

  {
    struct WeaponAccent {
      lg::Weapon weapon;
      lg::RenderColor color;
      std::string_view message;
    };
    constexpr std::array<WeaponAccent, 4> accents = {{
      {
        lg::Weapon::MachineGun,
        {218, 196, 116, 255},
        "machine gun viewmodel should use its ammo-feed accent",
      },
      {
        lg::Weapon::Shotgun,
        {188, 120, 84, 255},
        "shotgun viewmodel should use its wide pump accent",
      },
      {
        lg::Weapon::GrenadeLauncher,
        {112, 188, 90, 255},
        "grenade launcher viewmodel should use its drum accent",
      },
      {
        lg::Weapon::PlasmaGun,
        {95, 235, 210, 255},
        "plasma gun viewmodel should use its core accent",
      },
    }};

    for (const WeaponAccent& accent : accents) {
      const lg::DrawList2D overlay = lg::buildPerspectiveWeaponOverlay(
        1280,
        720,
        {},
        accent.weapon,
        lg::Weapon::LightningGun,
        1.0F,
        settings
      );
      bool foundAccent = false;
      for (const lg::DrawCommand2D& command : overlay.overlayCommands) {
        if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
          foundAccent =
            foundAccent ||
            (
              quad->color.red == accent.color.red &&
              quad->color.green == accent.color.green &&
              quad->color.blue == accent.color.blue &&
              quad->color.alpha == accent.color.alpha
            );
        }
      }
      failures += expect(foundAccent, accent.message);
    }
  }

  {
    const lg::DrawList2D switchingOverlay = lg::buildPerspectiveWeaponOverlay(
      1280,
      720,
      {},
      lg::Weapon::Railgun,
      lg::Weapon::LightningGun,
      0.2F,
      settings
    );
    const auto* quad = switchingOverlay.overlayCommands.empty()
      ? nullptr
      : std::get_if<lg::FilledQuad2D>(&switchingOverlay.overlayCommands.front());
    failures += expect(
      quad != nullptr && quad->points[0].y > 640.0F,
      "weapon switch should drop the outgoing viewmodel below the screen"
    );
  }

  {
    lg::RenderSettings crosshairSettings;
    crosshairSettings.crosshairRed = 20;
    crosshairSettings.crosshairGreen = 40;
    crosshairSettings.crosshairBlue = 60;
    crosshairSettings.crosshairHitRed = 120;
    crosshairSettings.crosshairHitGreen = 140;
    crosshairSettings.crosshairHitBlue = 160;
    crosshairSettings.crosshairHitAmount = 0.5F;

    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      crosshairSettings,
      {},
      {}
    );
    const auto* crosshairArm =
      ui.overlayCommands.empty()
        ? nullptr
        : std::get_if<lg::FilledQuad2D>(&ui.overlayCommands.front());
    failures += expect(
      crosshairArm != nullptr &&
        crosshairArm->color.red == 70 &&
        crosshairArm->color.green == 90 &&
        crosshairArm->color.blue == 110,
      "crosshair should blend to its hit-feedback color"
    );
  }

  {
    lg::HudRenderState damageHud;
    damageHud.damageNumbers.entries = {
      {11, 1, 0.0F, 0},
      {22, 1, 0.0F, 1},
      {33, 1, 0.0F, 2},
    };
    lg::RenderSettings damageSettings;
    damageSettings.crosshairEnabled = false;
    damageSettings.damageNumbersSize = 1.0F;
    damageSettings.damageNumbersOffsetX = 0.0F;
    damageSettings.damageNumbersOffsetY = -40.0F;
    const lg::DrawList2D damageUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      damageSettings,
      damageHud,
      {}
    );
    const lg::Text2D* first = findText(damageUi, "11");
    const lg::Text2D* second = findText(damageUi, "22");
    const lg::Text2D* third = findText(damageUi, "33");
    failures += expect(
      first != nullptr &&
        second != nullptr &&
        third != nullptr &&
        std::abs(first->position.x - second->position.x) <= 4.0F &&
        std::abs(second->position.x - third->position.x) <= 4.0F &&
        first->position.y < second->position.y &&
        second->position.y < third->position.y,
      "individual damage numbers should stack vertically near the aim point"
    );
  }

  {
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      hud,
      console
    );
    failures += expect(
      ui.commands.empty(),
      "screen UI should contain only unclipped overlay commands"
    );
    failures += expect(
      ui.overlayCommands.size() >= 13,
      "crosshair, scoreboard, and HUD should emit commands"
    );

    bool foundHealthLabel = false;
    bool foundScoreboardTitle = false;
    bool foundSpeed = false;
    std::array<bool, 7> foundWeaponLabels = {};
    std::size_t rightHudShapeCount = 0;
    constexpr std::array<std::string_view, 7> weaponLabels = {
      "MG",
      "SG",
      "GL",
      "RL",
      "LG",
      "RG",
      "PG",
    };
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        foundHealthLabel =
          foundHealthLabel || text->text == "ENEMY HP 50";
        foundScoreboardTitle =
          foundScoreboardTitle || text->text == "SCOREBOARD";
        foundSpeed = foundSpeed || text->text == "SPEED 320 UPS";
        for (std::size_t index = 0; index < weaponLabels.size(); ++index) {
          foundWeaponLabels[index] =
            foundWeaponLabels[index] ||
            (text->text == weaponLabels[index] && text->position.x > 1180.0F);
        }
      } else if (commandTouchesRightHud(command)) {
        ++rightHudShapeCount;
      }
    }
    failures += expect(
      !foundHealthLabel && foundScoreboardTitle && foundSpeed,
      "enemy health should move out of the static HUD"
    );
    failures += expect(
      rightHudShapeCount >= 40,
      "weapon HUD should draw compact per-slot icon silhouettes"
    );
    bool foundAllWeaponLabels = true;
    for (bool foundWeaponLabel : foundWeaponLabels) {
      foundAllWeaponLabels = foundAllWeaponLabels && foundWeaponLabel;
    }
    failures += expect(
      foundAllWeaponLabels,
      "selected weapon indicator should show all seven weapon slots"
    );

    constexpr std::array<lg::Weapon, 7> weapons = {{
      lg::Weapon::MachineGun,
      lg::Weapon::Shotgun,
      lg::Weapon::GrenadeLauncher,
      lg::Weapon::RocketLauncher,
      lg::Weapon::LightningGun,
      lg::Weapon::Railgun,
      lg::Weapon::PlasmaGun,
    }};

    for (std::size_t index = 0; index < weapons.size(); ++index) {
      hud.selectedWeapon = weapons[index];
      const lg::DrawList2D selectedUi = lg::buildScreenUi(
        1280,
        720,
        opponent,
        settings,
        hud,
        console
      );
      bool foundSelectedWeapon = false;
      bool iconOverlapsLabel = false;
      for (const lg::DrawCommand2D& command : selectedUi.overlayCommands) {
        if (const auto* text = std::get_if<lg::Text2D>(&command)) {
          foundSelectedWeapon =
            foundSelectedWeapon ||
            (
              text->text == weaponLabels[index] &&
              text->position.x > 1180.0F &&
              text->color.red == 255
            );
        } else if (
          commandOverlapsRect(
            command,
            1210.0F,
            174.0F + 66.0F * static_cast<float>(index),
            26.0F,
            14.0F
          )
        ) {
          iconOverlapsLabel = true;
        }
      }
      failures += expect(
        foundSelectedWeapon,
        "selected weapon indicator should mark every weapon slot"
      );
      failures += expect(
        !iconOverlapsLabel,
        "weapon HUD icons should stay clear of their text labels"
      );
    }
  }

  {
    lg::HudRenderState layoutHud;
    layoutHud.scoreboardOpen = true;
    layoutHud.scoreboardLines = {"SCOREBOARD", "NAME SCORE ACC DAMAGE", "> PLAYER 1"};
    layoutHud.centerLines = {"WAITING FOR PLAYERS", "1/6 PLAYERS CONNECTED"};
    layoutHud.centerOffsetY = -150.0F;
    const lg::DrawList2D scoreboardUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      layoutHud,
      console
    );
    layoutHud.scoreboardOpen = false;
    const lg::DrawList2D regularUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      layoutHud,
      console
    );
    const lg::Text2D* scoreboardTitle = findText(scoreboardUi, "SCOREBOARD");
    const lg::Text2D* scoreboardStatus =
      findText(scoreboardUi, "1/6 PLAYERS CONNECTED");
    const lg::Text2D* regularStatus =
      findText(regularUi, "1/6 PLAYERS CONNECTED");
    failures += expect(
      scoreboardTitle != nullptr &&
        scoreboardStatus != nullptr &&
        regularStatus != nullptr &&
        scoreboardStatus->position.y == regularStatus->position.y,
      "opening the scoreboard should not move the waiting status"
    );

    layoutHud.scoreboardOpen = true;
    layoutHud.centerOffsetY = -220.0F;
    const lg::DrawList2D raisedUi = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      layoutHud,
      console
    );
    const lg::Text2D* raisedStatus =
      findText(raisedUi, "1/6 PLAYERS CONNECTED");
    failures += expect(
      scoreboardTitle != nullptr &&
        raisedStatus != nullptr &&
        raisedStatus->position.y + 16.0F <=
          scoreboardTitle->position.y - 8.0F,
      "fixed waiting status position should stay above the scoreboard"
    );
  }

  {
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> remotePlayers = {};
    opponent.position = {10.0F, 0.0F, 0.0F};
    opponent.bounds.halfHeight = 0.9F;
    remotePlayers[1] = lg::RemotePlayerView{
      opponent,
      {},
      0.0F,
      0.5F,
      true,
      false,
      "RANGER",
    };
    settings.enemyHealthBarRed = 20;
    settings.enemyHealthBarGreen = 220;
    settings.enemyHealthBarBlue = 90;
    settings.enemyHealthBarWidth = 80.0F;
    settings.enemyHealthBarHeight = 8.0F;
    settings.enemyNameTagRed = 12;
    settings.enemyNameTagGreen = 34;
    settings.enemyNameTagBlue = 56;
    settings.enemyNameTagAlpha = 0.5F;
    settings.enemyNameTagScale = 2.0F;
    const lg::PerspectiveCamera camera =
      lg::makePerspectiveCamera({}, 0.0F, 0.0F, 90.0F, 16.0F / 9.0F);
    lg::Arena arena;
    const lg::DrawList2D bars = lg::buildFloatingHealthBars(
      1280,
      720,
      camera,
      arena,
      remotePlayers,
      settings,
      hud
    );
    failures += expect(
      bars.overlayCommands.size() == 4,
      "visible enemy should emit a floating name tag and health bar"
    );
    const auto* nameTag =
      std::get_if<lg::Text2D>(&bars.overlayCommands.front());
    failures += expect(
      nameTag != nullptr &&
        nameTag->text == "RANGER" &&
        nameTag->color.red == 12 &&
        nameTag->color.green == 34 &&
        nameTag->color.blue == 56 &&
        nameTag->color.alpha == 127 &&
        nameTag->scale == 2.0F,
      "floating enemy name tag should use configured text style"
    );
    const auto* fill =
      std::get_if<lg::FilledQuad2D>(&bars.overlayCommands.back());
    failures += expect(
      fill != nullptr &&
        fill->color.red == 20 &&
        fill->color.green == 220 &&
        fill->color.blue == 90 &&
        fill->color.alpha == 127 &&
        fill->points[1].x - fill->points[0].x == 40.0F,
      "floating health bar should use configured color, alpha, and health ratio"
    );

    arena.wallCount = 1;
    arena.walls[0] = {{4.0F, -1.0F, -1.0F}, {6.0F, 1.0F, 2.0F}};
    const lg::DrawList2D occludedEnemyBars = lg::buildFloatingHealthBars(
      1280,
      720,
      camera,
      arena,
      remotePlayers,
      settings,
      hud
    );
    failures += expect(
      occludedEnemyBars.overlayCommands.empty(),
      "occluded enemy should not emit a floating name tag or health bar"
    );

    remotePlayers[1].teammate = true;
    const lg::DrawList2D occludedTeammateBars = lg::buildFloatingHealthBars(
      1280,
      720,
      camera,
      arena,
      remotePlayers,
      settings,
      hud
    );
    failures += expect(
      occludedTeammateBars.overlayCommands.size() == 4,
      "occluded teammate should keep existing floating name tag and health bar behavior"
    );
  }

  {
    constexpr std::array<int, 5> visibleRightColumns = {5, 5, 5, 6, 5};
    for (std::size_t index = 0; index < visibleRightColumns.size(); ++index) {
      lg::HudRenderState countdownHud;
      countdownHud.countdownText = std::to_string(index + 1);
      const lg::DrawList2D ui = lg::buildScreenUi(
        1280,
        720,
        opponent,
        settings,
        countdownHud,
        {}
      );
      const lg::Text2D* text = nullptr;
      for (const lg::DrawCommand2D& command : ui.overlayCommands) {
        if (const auto* candidate = std::get_if<lg::Text2D>(&command)) {
          if (
            candidate->text == countdownHud.countdownText &&
            candidate->scale >= 10.0F
          ) {
            text = candidate;
          }
        }
      }
      const float visibleCenterX = text == nullptr
        ? 0.0F
        : text->position.x +
          static_cast<float>(visibleRightColumns[index] + 1) *
            text->scale * 0.5F;
      const float visibleCenterY = text == nullptr
        ? 0.0F
        : text->position.y + 3.5F * text->scale;
      failures += expect(
        text != nullptr &&
          std::abs(visibleCenterX - 640.0F) < 0.01F &&
          std::abs(visibleCenterY - 360.0F) < 0.01F,
        "countdown digit pixels should be centered in the screen"
      );
    }
  }

  {
    lg::HudRenderState chatHud;
    chatHud.chatLines.push_back({
      0,
      "This is a long message that wraps onto continuation rows",
      "Zap Witch"
    });
    const lg::ChatTextLayout layout =
      lg::buildChatTextLayout(420, 720, chatHud);
    bool foundPrefixRow = false;
    bool foundContinuationRow = false;
    for (const lg::ChatLayoutRow& row : layout.rows) {
      failures += expect(
        row.text.size() <= 24U,
        "wrapped chat rows should fit available columns"
      );
      foundPrefixRow = foundPrefixRow || row.text.rfind("Zap Witch:", 0U) == 0U;
      foundContinuationRow =
        foundContinuationRow ||
        (row.continuation && row.text.rfind("           ", 0U) == 0U);
      failures += expect(
        !row.continuation || row.text.find("Zap Witch:") == std::string::npos,
        "chat continuation rows should not repeat the speaker prefix"
      );
    }
    failures += expect(
      foundPrefixRow && foundContinuationRow,
      "chat layout should wrap with continuation indentation"
    );

    chatHud.chatLines = {{
      {1, "supercalifragilisticexpialidocious", ""}
    }};
    const lg::ChatTextLayout longWordLayout =
      lg::buildChatTextLayout(300, 720, chatHud);
    for (const lg::ChatLayoutRow& row : longWordLayout.rows) {
      failures += expect(
        row.text.size() <= 17U,
        "long chat words should split before overflowing"
      );
    }
  }

  {
    lg::HudRenderState inputHud;
    inputHud.chatInputOpen = true;
    inputHud.chatInput = "one two three four five six";
    inputHud.chatCursorIndex = inputHud.chatInput.size();
    const lg::ChatTextLayout inputLayout =
      lg::buildChatTextLayout(420, 720, inputHud);
    failures += expect(
      inputLayout.inputRows.size() >= 2U &&
        inputHud.chatInput.find('\n') == std::string::npos,
      "long active chat input should wrap visually without changing logical input"
    );
    failures += expect(
      inputLayout.inputRows.front().text.rfind("CHAT: ", 0U) == 0U,
      "wrapped chat input should use CHAT prefix on the first row"
    );
    for (std::size_t index = 1; index < inputLayout.inputRows.size(); ++index) {
      failures += expect(
        inputLayout.inputRows[index].text.rfind("      ", 0U) == 0U &&
          inputLayout.inputRows[index].text.find("CHAT:") == std::string::npos,
        "chat input continuation rows should align after the prefix"
      );
    }

    const std::size_t continuationCursor = inputLayout.inputRows[1].inputBegin;
    const lg::ScreenPoint continuationCaret =
      lg::chatInputCursorPosition(
        inputLayout,
        inputHud.chatInput,
        continuationCursor
      );
    failures += expect(
      std::abs(continuationCaret.y - inputLayout.inputRows[1].y) < 0.01F,
      "chat input caret after a wrap should render on the continuation row"
    );

    const std::size_t clickedOffset = lg::chatInputOffsetAt(
      inputLayout,
      inputHud.chatInput,
      inputLayout.inputRows[1].x +
        static_cast<float>(inputLayout.inputRows[1].contentColumn + 2U) *
          inputLayout.characterWidth,
      inputLayout.inputRows[1].y + 2.0F
    );
    failures += expect(
      clickedOffset == inputLayout.inputRows[1].inputBegin + 2U,
      "clicking a chat input continuation row should map to its UTF-8-safe cursor offset"
    );

    inputHud.chatHasSelection = true;
    inputHud.chatSelectionAnchor = 1U;
    inputHud.chatSelectionFocus = inputLayout.inputRows[1].inputBegin + 2U;
    const lg::DrawList2D selectedInputUi = lg::buildScreenUi(
      420,
      720,
      opponent,
      settings,
      inputHud,
      {}
    );
    int selectionHighlights = 0;
    for (const lg::DrawCommand2D& command : selectedInputUi.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        if (
          quad->color.red == 58 &&
          quad->color.green == 118 &&
          quad->color.blue == 188
        ) {
          ++selectionHighlights;
        }
      }
    }
    failures += expect(
      selectionHighlights >= 2,
      "chat input selection spanning a wrap should highlight both visual rows"
    );

    lg::HudRenderState swedishInput;
    swedishInput.chatInputOpen = true;
    swedishInput.chatInput = "åäöÅÄÖ åäöÅÄÖ åäöÅÄÖ";
    swedishInput.chatCursorIndex = swedishInput.chatInput.size();
    const lg::ChatTextLayout swedishLayout =
      lg::buildChatTextLayout(260, 720, swedishInput);
    failures += expect(
      swedishLayout.inputRows.size() >= 2U,
      "Swedish active chat input should wrap by UTF-8 codepoint"
    );
    const std::size_t swedishOffset = lg::chatInputOffsetAt(
      swedishLayout,
      swedishInput.chatInput,
      swedishLayout.inputRows.back().x +
        static_cast<float>(swedishLayout.inputRows.back().contentColumn + 1U) *
          swedishLayout.characterWidth,
      swedishLayout.inputRows.back().y + 2.0F
    );
    failures += expect(
      lg::isUtf8Boundary(swedishInput.chatInput, swedishOffset),
      "wrapped Swedish chat input hit testing should stay on UTF-8 boundaries"
    );
  }

  {
    console.open = true;
    console.lines = {"first", "second"};
    console.input = "r_vsync 0";
    console.cursorIndex = console.input.size();
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      hud,
      console
    );
    const auto* prompt =
      std::get_if<lg::Text2D>(&ui.overlayCommands.back());
    failures += expect(
      prompt != nullptr &&
        prompt->text == "] r_vsync 0_" &&
        prompt->color.red == 255,
      "console prompt should render last and above the rest of the UI"
    );
  }

  {
    lg::ConsoleRenderState narrowConsole;
    narrowConsole.open = true;
    narrowConsole.lines = {"alpha beta gamma"};
    narrowConsole.input = "wrap input";
    narrowConsole.cursorIndex = narrowConsole.input.size();
    const lg::DrawList2D ui = lg::buildScreenUi(
      180,
      240,
      opponent,
      settings,
      {},
      narrowConsole
    );

    bool foundFirstOutputWrap = false;
    bool foundSecondOutputWrap = false;
    bool foundFirstPromptWrap = false;
    bool foundSecondPromptWrap = false;
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* text = std::get_if<lg::Text2D>(&command)) {
        foundFirstOutputWrap =
          foundFirstOutputWrap || text->text == "alpha beta";
        foundSecondOutputWrap =
          foundSecondOutputWrap || text->text == "gamma";
        foundFirstPromptWrap =
          foundFirstPromptWrap || text->text == "] wrap";
        foundSecondPromptWrap =
          foundSecondPromptWrap || text->text == "input_";
        if (
          text->color.red == 215 ||
          (
            text->color.red == 255 &&
            text->color.green == 255 &&
            text->color.blue == 255
          )
        ) {
          failures += expect(
            text->text.size() <= 10U,
            "wrapped console text should fit the available character columns"
          );
        }
      }
    }
    failures += expect(
      foundFirstOutputWrap && foundSecondOutputWrap,
      "console output should wrap to the available width"
    );
    failures += expect(
      foundFirstPromptWrap && foundSecondPromptWrap,
      "console prompt should wrap to the available width"
    );
  }

  {
    lg::ConsoleRenderState selectableConsole;
    selectableConsole.open = true;
    selectableConsole.lines = {"alpha beta"};
    selectableConsole.input = "copy me";
    selectableConsole.cursorIndex = selectableConsole.input.size();

    const lg::ConsoleTextLayout layout =
      lg::buildConsoleTextLayout(1280, 720, selectableConsole);
    const std::size_t selectionStart =
      lg::consoleTextOffsetAt(layout, 10.0F, 10.0F);
    const std::size_t selectionEnd =
      lg::consoleTextOffsetAt(layout, 10.0F + 5.0F * 16.0F, 10.0F);
    failures += expect(
      lg::consoleSelectedText(layout, selectionStart, selectionEnd) == "alpha",
      "console mouse selection should copy selected visible text"
    );

    selectableConsole.hasSelection = true;
    selectableConsole.selectionAnchor = selectionStart;
    selectableConsole.selectionFocus = selectionEnd;
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      {},
      selectableConsole
    );
    bool foundSelectionHighlight = false;
    for (const lg::DrawCommand2D& command : ui.overlayCommands) {
      if (const auto* quad = std::get_if<lg::FilledQuad2D>(&command)) {
        foundSelectionHighlight =
          foundSelectionHighlight ||
          (
            quad->color.red == 58 &&
            quad->color.green == 118 &&
            quad->color.blue == 188 &&
            quad->color.alpha == 170
          );
      }
    }
    failures += expect(
      foundSelectionHighlight,
      "console selection should render a highlight behind selected text"
    );
  }

  {
    lg::ConsoleRenderState cursorConsole;
    cursorConsole.open = true;
    cursorConsole.input = "r_vsync 0";
    cursorConsole.cursorIndex = 2U;
    const lg::DrawList2D ui = lg::buildScreenUi(
      1280,
      720,
      opponent,
      settings,
      {},
      cursorConsole
    );
    const auto* prompt =
      std::get_if<lg::Text2D>(&ui.overlayCommands.back());
    failures += expect(
      prompt != nullptr && prompt->text == "] r__vsync 0",
      "console prompt cursor should render at the tracked input position"
    );
  }

  return failures == 0 ? 0 : 1;
}
