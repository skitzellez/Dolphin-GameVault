// Copyright 2026 Dolphin for Game Vault (Skitzelles)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace GameVault
{
// Picks the controller a Wii game is played with (Classic Controller, GameCube controller, Wii Remote + Nunchuk or
// Wii Remote alone) from Sys/GameVault/wii-controls.txt and sets every connected Xbox pad up for it, for this run
// only. Called each time a Wii title starts, after the controller settings are loaded. Xbox builds only.
void AutoPad(const std::string& game_id);
}  // namespace GameVault
