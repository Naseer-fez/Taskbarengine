# TaskbarEngine

TaskbarEngine is a high-performance C-based engine for modifying the Windows 11 Taskbar. It uses DirectComposition for 0-latency rendering and runs entirely in the Explorer process space to ensure maximum performance.

## Features

- **High Performance:** 0-latency rendering via DirectComposition.
- **Dynamic Configuration:** Hot-reload settings without restarting Explorer.
- **Customizable:** Easily extendable plugin system for modifying the taskbar.

## Installation

Download the `TaskbarEngine-v1.0.0.zip` release and extract it to a directory of your choice.

To install, simply run `TaskbarEngine.exe`. This will:
1. Inject the engine into Explorer.
2. Place a settings icon in your system tray.
3. Register a Task Scheduler entry to start automatically on logon.

## Configuration

Double-click the tray icon to open the Settings GUI.
You can configure plugin behavior dynamically. Settings are saved to `%LOCALAPPDATA%\TaskbarEngine\config.jsonc` and hot-reloaded automatically.

## Uninstalling

Run the `uninstall.ps1` script to remove the scheduled task and configuration files, then delete the folder.
