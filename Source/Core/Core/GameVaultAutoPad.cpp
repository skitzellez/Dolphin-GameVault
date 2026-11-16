// Copyright 2026 Dolphin for Game Vault (Skitzelles)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/GameVaultAutoPad.h"

#if WINRT_XBOX

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include <fmt/format.h>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/ConfigManager.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/InputConfig.h"

namespace GameVault
{
// "<ID6> <pick>" lines; a game missing under its own ID takes the pick of another region of it (same first 3)
static char LookUp(const std::string& id)
{
  static std::unordered_map<std::string, char> by_id, by_game;
  static bool loaded = false;
  if (!loaded)
  {
    loaded = true;
    std::ifstream f(File::GetSysDirectory() + "GameVault/wii-controls.txt");
    std::string line;
    while (std::getline(f, line))
    {
      if (line.size() < 8 || line[0] == '#')
        continue;
      by_id[line.substr(0, 6)] = line[7];
      by_game.emplace(line.substr(0, 3), line[7]);
    }
    NOTICE_LOG_FMT(CORE, "Game Vault auto controller: {} games in the list", by_id.size());
  }
  if (auto it = by_id.find(id.substr(0, 6)); it != by_id.end())
    return it->second;
  if (auto it = by_game.find(id.substr(0, 3)); id.size() >= 3 && it != by_game.end())
    return it->second;
  return 0;
}

static void Note(const std::string& line)
{
  NOTICE_LOG_FMT(CORE, "Game Vault auto controller: {}", line);
  File::WriteStringToFile(File::GetUserPath(D_LOGS_IDX) + "gv-autopad.txt",
                          "Game Vault auto controller: " + line + "\n");
}

static int XboxPads()
{
  int pads = 0;
  for (const auto& name : g_controller_interface.GetAllDeviceStrings())
    if (name.starts_with("WGInput/"))
      pads++;
  return std::clamp(pads, 1, 4);  // the pads may not be listed yet this early: player 1 always gets one
}

void AutoPad(const std::string& game_id)
{
  if (game_id.size() < 4 || game_id == "00000000")
  {
    Note("no game id (\"" + game_id + "\")");
    return;
  }
  if (File::Exists(File::GetUserPath(D_CONFIG_IDX) + "gv-autopad-off.txt"))
  {
    Note("off (Config/gv-autopad-off.txt)");
    return;
  }
  // the player switched the Wii Remote off to play with a GameCube controller (Wii Remote port 1 = None)
  if (Config::GetBase(Config::GetInfoForWiimoteSource(0)) == WiimoteSource::None)
  {
    Note("Wii Remote 1 is set to None by the player, left as set");
    return;
  }
  // a game input profile from the game's settings wins
  if (SConfig::GetInstance().LoadGameIni().GetOrCreateSection("Controls")->Exists("WiimoteProfile1"))
  {
    Note("the game's settings name a Wii Remote profile, left as set");
    return;
  }
  const char pick = LookUp(game_id);
  if (!pick)
  {
    Note(fmt::format("{} not in the list ({}GameVault/wii-controls.txt), left as set", game_id, File::GetSysDirectory()));
    return;
  }

  const int pads = XboxPads();
  std::string done;
  if (pick == 'G')
  {
    // GameCube controller: the Wii Remotes are switched off so no press counts twice
    for (int i = 0; i < 4; i++)
    {
      Config::SetCurrent(Config::GetInfoForWiimoteSource(i), WiimoteSource::None);
      if (i < pads)
        Config::SetCurrent(Config::GetInfoForSIDevice(i), SerialInterface::SIDEVICE_GC_CONTROLLER);
    }
    done = "GameCube controller";
  }
  else
  {
    // Wii Remote kinds: the GameCube ports are switched off so no press counts twice
    for (int i = 0; i < 4; i++)
      Config::SetCurrent(Config::GetInfoForSIDevice(i), SerialInterface::SIDEVICE_NONE);
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    for (int i = 0; i < pads; i++)
    {
      auto* remote = static_cast<WiimoteEmu::Wiimote*>(Wiimote::GetConfig()->GetController(i));
      done += fmt::format("{}player {}: {}", i ? "; " : "", i + 1, remote->GvAutoSetup(pick, g_controller_interface));
      Config::SetCurrent(Config::GetInfoForWiimoteSource(i), WiimoteSource::Emulated);
    }
  }
  Note(fmt::format("{} -> {} ({} pad(s))", game_id, done, pads));
}
}  // namespace GameVault

#else

namespace GameVault
{
void AutoPad(const std::string&)
{
}
}  // namespace GameVault

#endif
