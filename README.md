<div align="center">

<img src="icon.png" alt="Boost11 icon" width="128" height="128">

# Boost11

**Switch Windows between *Balanced* and *High performance* in one click.**
A tiny, dependency-free power mode switcher written in C++.

[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4?logo=windows11&logoColor=white)](#requirements)
[![License](https://img.shields.io/github/license/morrisonion/Boost11)](LICENSE)
[![Stars](https://img.shields.io/github/stars/morrisonion/Boost11?style=flat)](https://github.com/morrisonion/Boost11/stargazers)
[![Last commit](https://img.shields.io/github/last-commit/morrisonion/Boost11)](https://github.com/morrisonion/Boost11/commits)

</div>

---

## Features

- **Two modes, one toggle** – *Balanced* ⇄ *High performance*, applied instantly via the Windows power API (no admin rights, no console window).
- **Pick your workflow** in a small first-run wizard:
  - **Tray icon** – left- or right-click opens a minimal dark menu to choose the mode.
  - **Desktop shortcut** – the wizard creates `Boost11.lnk`; double-click it to toggle. Nothing stays running in the background.
- **Visual feedback** – whenever the mode changes, the icon of the new mode appears in the center of the screen and slowly fades out.
- **Automatic plan detection** – power plan GUIDs are read from `powercfg -l` (language independent). If *High performance* is missing (e.g. on Modern Standby devices), Boost11 creates it for you.
- **Modern look without frameworks** – dark Windows 11 style, rounded corners, smooth animations and per-monitor DPI scaling, all drawn with plain GDI+. No XAML, no C#, no Visual Studio.
- **Translations** – English is built in;contribute by making an issue or a pull request.
- **Tiny** – one static executable of 650 KB, icons are embedded.

## Requirements

- Windows 11/Windows 10(not recommended)
- Nothing else. No runtime, no installer.



The wizard lets you choose the language and whether Boost11 should start with Windows (tray mode only).

Settings are stored in `%APPDATA%\Boost11\config.ini`.

> If you move `Boost11.exe`, run the wizard again so the desktop shortcut points to the new location.

## Building

No Visual Studio required – any MinGW-w64 toolchain works (e.g. [w64devkit](https://github.com/skeeto/w64devkit) or [WinLibs](https://winlibs.com/)).

```bat
git clone https://github.com/YOUR_USERNAME/Boost11.git
cd Boost11
build.bat
```

`build.bat` compiles the resources (`windres`) and links a static `Boost11.exe` with `-lgdiplus -lgdi32 -luser32 -lshell32 -lshlwapi -lole32 -luuid -lpowrprof -ldwmapi -ladvapi32 -lshcore`.

## Project structure

```
Boost11/
├─ icon.png               README icon
├─ build.bat              build script (g++ + windres)
├─ boost11.rc             embeds icons, manifest and version info
├─ boost11.manifest       PerMonitorV2 DPI awareness
├─ src/
│  ├─ main.cpp            the whole application
│  └─ resource.h
├─ assets/                images embedded into the executable
└─ i18n/                  translation files (loaded at runtime)
```


## How it works

- Plans are discovered by running `powercfg -l` (hidden) and matching the well-known GUIDs, with name matching as fallback. The result is cached in `config.ini`.
- Modes are switched with `PowerSetActiveScheme`; the active plan is read with `PowerGetActiveScheme`.
- The overlay is a click-through layered window updated with `UpdateLayeredWindow` for smooth per-pixel alpha fading.
- All UI (wizard, tray menu, language dropdown) is custom-drawn with GDI+ and animated with a simple easing timer.

## License

This project is licensed under the GNUgplv3
