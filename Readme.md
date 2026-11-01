<p align="center">
  <a href="https://github.com/skitzellez/xbox-game-vault"><img src="https://raw.githubusercontent.com/skitzellez/xbox-game-vault/main/GameVault/art/logo-banner.png" alt="Game Vault" width="640"></a>
</p>

<h1 align="center">Dolphin for Game Vault</h1>

<p align="center">
  <b>GameCube and Wii games on Xbox Series X|S in Dev Mode.</b><br>
  Dolphin for Xbox, built to work hand in hand with <a href="https://github.com/skitzellez/xbox-game-vault">Game Vault</a>: pick a game, it plays, and you are back in Game Vault when you quit.
</p>

<!-- gv-buttons:start -->
<p align="center">
  <a href="https://github.com/skitzellez/Dolphin-GameVault/releases/latest"><img alt="DOWNLOAD Dolphin" src="https://img.shields.io/badge/DOWNLOAD-Dolphin-107C10?style=for-the-badge&logo=xbox&logoColor=white"></a>
  &nbsp;
  <a href="https://github.com/skitzellez/xbox-game-vault"><img alt="Get Game Vault" src="https://img.shields.io/badge/GET-Game%20Vault-107C10?style=for-the-badge&logo=xbox&logoColor=white"></a>
  &nbsp;
  <a href="https://discord.gg/RmhhWgguvw"><img alt="Join the Game Vault Discord" src="https://img.shields.io/badge/JOIN-Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white"></a>
  &nbsp;
  <a href="https://www.patreon.com/Skitzelles"><img alt="Support on Patreon" src="https://img.shields.io/badge/SUPPORT-Patreon-F96854?style=for-the-badge&logo=patreon&logoColor=white"></a>
</p>
<!-- gv-buttons:end -->

> [!IMPORTANT]
> This is an **unofficial build**. It is not made or supported by the Dolphin team or XboxEmulationHub, so please report
> problems with it **here or on the [Game Vault Discord](https://discord.gg/RmhhWgguvw)**, not to them.

> [!NOTE]
> **No games or Nintendo files are included.** Play only games you own.

## ✨ What's different from the original

| | |
|---|---|
| 🎯 **Starts from Game Vault** | Pick a GameCube or Wii game in Game Vault and it starts here; quit and you are back in Game Vault. |
| 🎮 **Controller ready** | Your Xbox controller is set up for each game, nothing to map. |
| 🔗 **Fixes for Game Vault** | Games started from Game Vault load the right way, with their saves kept. |

## 🚀 Getting started

The easy way: install **[Game Vault](https://github.com/skitzellez/xbox-game-vault)**. Its setup installs this emulator, and Game Vault
offers each new version on its update screen (press **RT**).

By hand: download the `.appx` from [Releases](https://github.com/skitzellez/Dolphin-GameVault/releases/latest) and add it in Device Portal
(**My games & apps → Add**), with the dependencies it asks for.

<!-- gv-family:start -->
## 🎮 The Game Vault family

Game Vault is one launcher plus emulators built to work with it. **Install [Game Vault](https://github.com/skitzellez/xbox-game-vault) and it sets these up for you**; press **RT** in Game Vault to update them all.

**Apps**

| Project | What it is | Runs on |
|---|---|---|
| [Game Vault](https://github.com/skitzellez/xbox-game-vault) | The launcher: plug in a USB drive, pick a game, play. Sets up everything below by itself. | Xbox Series X&#124;S |
| [Game Vault for Android](https://github.com/skitzellez/game-vault-android) | The same game library for phones, tablets and Android TV (beta). | Android |
| [Game Vault for iPhone](https://github.com/skitzellez/game-vault-ios) | The Game Vault look on iPhone (early test). | iPhone |

**Emulators built for Game Vault**

| Project | What it is | Runs on |
|---|---|---|
| [xemu for Xbox Dev Mode](https://github.com/skitzellez/xemu-uwp) | Original Xbox | Xbox Series X&#124;S |
| [Xenia for Game Vault](https://github.com/skitzellez/Xenia-GameVault) | Xbox 360 | Xbox Series X&#124;S |
| [XBSX2 for Game Vault](https://github.com/skitzellez/XBSX2-GameVault) | PlayStation 2 | Xbox Series X&#124;S |
| [**Dolphin for Game Vault**](https://github.com/skitzellez/Dolphin-GameVault) 📍 *you are here* | GameCube and Wii | Xbox Series X&#124;S |
| [Game Vault · Wii U (Cemu)](https://github.com/skitzellez/Cemu-GameVault) | Wii U | Xbox Series X&#124;S |
| [RetroArch for Game Vault](https://github.com/skitzellez/RetroArch-GameVault) | PlayStation, Dreamcast, arcade and classic systems | Xbox Series X&#124;S, Android |
| [Game Vault · Flash](https://github.com/skitzellez/Flash-GameVault) | Flash games | Xbox Series X&#124;S |
| [Gambatte for Game Vault](https://github.com/skitzellez/Gambatte-GameVault) | Game Boy link cable in online rooms | Xbox Series X&#124;S, Android |
| [X1 BOX for Game Vault](https://github.com/skitzellez/x1box-gamevault) | Original Xbox | Android |

**Extras**

| Project | What it is | Runs on |
|---|---|---|
| [Free games](https://github.com/skitzellez/game-vault-free-games) | Freeware and shareware for the FREE GAMES tab | Game Vault |
| [Fallout 1in2 for Xbox](https://github.com/skitzellez/Fallout1in2-Xbox) | The original Fallout, made for the controller | Xbox Series X&#124;S |
| [Game Vault updates](https://github.com/skitzellez/game-vault-updates) | Update data Game Vault reads when it starts | Game Vault |

**💬 Help, news and bug reports:** the [Game Vault Discord](https://discord.gg/RmhhWgguvw) (or this repo's Issues).
Please don't take problems with these builds to the original emulator teams: they don't make or support them.
<!-- gv-family:end -->

## ❤️ Support

Game Vault and its emulators are free and stay free. If you'd like to help, you can support it on **[Patreon](https://www.patreon.com/Skitzelles)**. ❤️

## 📜 Credits and licence

Based on **[Dolphin](https://github.com/dolphin-emu/dolphin)** and its Xbox (UWP) port by XboxEmulationHub, GPL-2.0-or-later: see [COPYING](COPYING).
Game Vault changes by **[Skitzelles](https://github.com/skitzellez)**.

---

<details>
<summary><b>DolphinUWP</b> (the original project's README)</summary>

# DolphinUWP - A GameCube and Wii Emulator

![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/SternXD/dolphin/buildwinrt.yml)
[![Discord Server](https://img.shields.io/discord/1007582798598647889?color=%235CA8FA&label=Xbox%20Emulation%20Hub&logo=discord&logoColor=white)]([https://discord.com/invite/emulation-collective-1007582798598647889](https://discord.gg/emulation-collective-1007582798598647889))

Dolphin is an emulator for running GameCube and Wii games on Windows,
Linux, macOS, recent Android devices, and UWP/Xbox (Because of this fork). It's licensed under the terms
of the GNU General Public License, version 2 or later (GPLv2+).

Please read the [FAQ](https://dolphin-emu.org/docs/faq/) before using Dolphin.

## System Requirements

### UWP (Universal Windows Platform)

Recommended: Xbox Series S or Xbox Series X

Minimum: Xbox One, Xbox One S, or Xbox One X (expect games to not run well on these platforms) 

## Building for Windows

Use the solution file `Source/dolphin-emu.sln` to build DolphinUWP on Windows.
DolphinUWP targets the latest MSVC shipped with Visual Studio or Build Tools.
Other compilers might be able to build DolphinUWP on Windows but have not been
tested and are not recommended to be used. Git and latest Windows SDK must be
installed when building. If you need more help go to the building guide [here](https://wiki.xbdev.store/en/CompilingDolphin)

Make sure to pull submodules before building:
```sh
git submodule update --init --recursive
```

The "Release" solution configuration includes performance optimizations for the best user experience but complicates debugging Dolphin.
The "Debug" solution configuration is significantly slower, more verbose and less permissive but makes debugging Dolphin easier.

Credits:

[Dolphin](https://dolphin-emu.org/): for their amazing work on [Dolphin Emulator](https://dolphin-emu.org/)

[SirMangler](https://github.com/SirMangler): for their amazing work on porting Dolphin for UWP

[worleydl](https://github.com/worleydl): For his [oct2024-rebase](https://github.com/worleydl/dolphin-color/tree/oct2024-rebase) branch (it helped so much)

Dolphin Emulator is licensed under GPLv2+ and is not associated with Nintendo.

</details>
