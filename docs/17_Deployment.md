# 17 — Deployment & Packaging

> **Source of Truth**: `docs/design_decisions.md`  
> **Component**: `Scripts/` (`package.ps1`, `uninstall.ps1`), `App/src/scheduler.cpp`

---

## 1. Portable ZIP Package Structure

TaskbarEngine is distributed as a portable, standalone ZIP archive (`TaskbarEngine-v1.0.0.zip`):

```
TaskbarEngine-v1.0.0/
├── TaskbarEngine.exe               # Tray host executable
├── TaskbarEngineSettings.exe       # WinUI 3 settings GUI executable
├── EngineDLL.dll                   # In-process engine DLL
├── config.jsonc                    # Default configuration template
├── README.md                       # User guide
├── LICENSE                         # MIT License
├── uninstall.ps1                   # Uninstallation script
├── Modules/                        # Plugin folder
│   ├── taskbar_resize.dll          # Resize plugin
│   └── icon_hover.dll              # Hover magnification plugin
├── Docs/                           # Full Markdown documentation series
└── [WinAppSDK Runtime Files]       # Self-contained Microsoft.WindowsAppRuntime DLLs
```

---

## 2. Packaging Automation (`Scripts/package.ps1`)

Generate the production release package with a single command:

```powershell
powershell -File Scripts\package.ps1 -BuildDir build_msvc -DestinationZip "..\TaskbarEngine-v1.0.0.zip"
```

### Packaging Steps
1. Validates Release binaries in `build_msvc/bin/`.
2. Creates a clean staging directory.
3. Copies `TaskbarEngine.exe`, `TaskbarEngineSettings.exe`, `EngineDLL.dll`, and `Modules/*.dll`.
4. Copies all self-contained Windows App SDK runtime assets and PRIs.
5. Copies `Config/default_config.jsonc` (renamed to `config.jsonc`), `README.md`, `LICENSE`, `uninstall.ps1`, and `Docs/`.
6. Compresses the staging directory into `TaskbarEngine-v1.0.0.zip` and generates a `.sha256` checksum file.

---

## 3. Auto-Start & Task Scheduler Integration (`scheduler.cpp`)

TaskbarEngine registers itself to launch upon user logon using the Windows Task Scheduler COM 2.0 API (`ITaskService`):

- **Task Name**: `TaskbarEngine_Logon`
- **Trigger**: `TASK_TRIGGER_LOGON` bound to the active interactive user token.
- **Run Level**: `TASK_RUNLEVEL_LUA` (Medium Integrity). Runs as standard user without prompting for UAC elevation to ensure same-desktop hook injection into Explorer.
- **CLI Commands**:
  ```powershell
  # Register auto-start task
  TaskbarEngine.exe --install

  # Remove auto-start task
  TaskbarEngine.exe --uninstall
  ```

---

## 4. Uninstallation (`Scripts/uninstall.ps1`)

Executing `uninstall.ps1`:
1. Terminates `TaskbarEngine.exe` and `TaskbarEngineSettings.exe`.
2. Removes the `TaskbarEngine_Logon` scheduled task via `Unregister-ScheduledTask`.
3. Deletes the configuration folder `%LOCALAPPDATA%\TaskbarEngine`.
4. Allows clean directory deletion.
