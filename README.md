  Fixes Starsector permanently losing audio when Bluetooth headphones
  disconnect or reconnect mid-game. Once it happens, vanilla OpenAL has
  no way to recover without restarting the game this proxy detects the
  Windows default audio device changing and live-switches without
  dropping the audio context.

  Works on both vanilla Starsector and Java 27 instances (Mikohime) the installer
  auto-detects which one(s) you have.

  **Windows only.** Relies on Win32/COM device APIs throughout - won't
  run on Linux or macOS.

  ### Install
  1. Download `openal audio fixer.7z` below and extract it into your
     Starsector folder, so it sits next to `starsector-core\`.
  2. Run `install.bat` inside it.
  3. For vanilla, launch via the new "Launch Vanilla - BT Fix.bat" instead
     of starsector.exe (required for the fix to actually engage). Miko
     keeps using its existing launcher as usual.

  See INSTRUCTIONS.txt in the package for full manual-install/uninstall
  steps and troubleshooting.
