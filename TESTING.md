# Test harness

How to build, run and observe the LAS mod without manual play.

## Emulator: Ryubing (Ryujinx fork)

- **Binary:** `D:\Games\Ryujinx\Ryujinx.exe` (Ryubing Canary).
- **Game:** `D:\Games\Switch\The Legend of Zelda Links Awakening [01006BB00C6F0000][v0].nsp` (1.0.0, no update installed).
- **Sandbox data folder:** `D:\DEV\LASAP-sandbox\ryujinx`. A copy of the user's Ryujinx data (keys, firmware, saves). Tests never use `%APPDATA%\Ryujinx`.
  - It contains `prod.keys`: never commit it, and never copy it into this repo.
- **Mods load from** `<sandbox>\sdcard\atmosphere\contents\01006BB00C6F0000\{exefs,romfs}`.
- **LASR runtime config:** `<sandbox>\sdcard\config\lasr-exl\config.ini`.

### Headless launch

```powershell
D:\Games\Ryujinx\Ryujinx.exe --no-gui --root-data-dir D:\DEV\LASAP-sandbox\ryujinx --graphics-backend OpenGl --input-id-1 0 --enable-keyboard --skip-user-profiles-manager --ignore-controller-applet --audio-volume 0 "<path to nsp>"
```

- **Use OpenGL.** Headless Vulkan crashed on boot with an access violation in `SamplerHolder..ctor` (observed 2026-10-12, Canary 1.3.340, run with `--use-main-config`).
- **Stopping it:** kill the process by its PID. Never kill by process name, because the user may have their own Ryujinx open.
- **Guest networking:** add `--enable-internet-connection` if the guest needs it.
- **GDB stub:** add `--enable-gdb-stub --gdb-stub-port <port>`. The local client is `C:\msys64\mingw64\bin\gdb-multiarch.exe`. Ryubing's stub has no watchpoints.

## Observing the game

- **Guest log.** `svcOutputDebugString` from exlaunch (`[exlaunch|...]` lines) is written to `D:\Games\Ryujinx\Logs\Ryujinx_*.log`.
  - The log goes to the exe's folder even with `--root-data-dir`.
  - Pick the newest file by write time.
- **Screenshots.** Capture the emulator window by PID with `PrintWindow` (flag `PW_RENDERFULLCONTENT`). This works while the window is unfocused or covered.
- **Game state and input.** Through the mod's own debug socket commands:
  - read and write flags and inventory
  - give items
  - warp
  - inject buttons through the existing `GetNpadState` hook

  This keeps tests independent of the emulator and needs no window focus.
- **No savestates.** Start from save files placed in `<sandbox>\bis\user\save`.

## Building the exlaunch module

- **Toolchain:** the Docker image `devkitpro/devkita64:latest` (Docker Desktop).
- **Source:** the module is `Owen-Splat/exlaunch`; a reference clone is at `reference/LAS-exlaunch`.

## Why not Eden

Eden (yuzu fork) works but offers no decisive advantage (assessed 2026-10-12):
- **Proven setup.** Randovania's Dread exlaunch socket mod officially supports only Ryujinx, and the LASR setup docs follow Ryujinx.
- **Sockets.** Eden defaults to `airplane_mode=true`, which breaks TCP sockets until that setting is changed.
- **TAS input.** Eden's TAS player runs only in the Qt frontend and starts by hotkey.

Eden would be worth adding for GDB watchpoints or file-scripted TAS input.
