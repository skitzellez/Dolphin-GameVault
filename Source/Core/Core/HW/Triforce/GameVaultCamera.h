// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Game Vault: a built-in stand-in for the Mario Kart Arcade GP cabinets' player camera (namcam2).
// The game asks the camera, on the cabinet network at 192.168.29.104-107 port 80, for "/img.jpg"
// over HTTP 1.0 and shows "CAMERA ERROR / Please call an attendant" when nothing answers. This
// serves a still 320x240 picture (Sys/Resources/default_camera_feed.jpg) from 127.0.0.1 on a free
// port; AMMediaboard sends the camera addresses there. Same idea as Dolphin PR #14393 (integrated
// camera, not merged yet), without a real webcam.

#pragma once

#include <optional>
#include <string>

#include "Common/CommonTypes.h"

namespace Triforce::GameVaultCamera
{
// Starts the server if it isn't running. Safe to call more than once.
void Start();
void Stop();

// "192.168.29.104-107:80=127.0.0.1:<port>" while the server runs, else nothing.
std::optional<std::string> GetRedirection();
}  // namespace Triforce::GameVaultCamera
