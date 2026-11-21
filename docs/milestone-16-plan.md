# Milestone 16 plan - Android version

**Tier A. Status: completed and accepted (2026-11-21).** Personal Android
build `1.0.0-m16.4`. The maintainer passed all five checks in the m16.2
Samsung play-test and requested closure. Review fixes and the final APK were
verified on the same Galaxy S24 Ultra (SM-S928B).

## What shipped

- Android 7.0 and later on 32-bit and 64-bit ARM run the same C++ game as
  Windows. The personal APK contains the engine and its license notices.
  The player supplies a US Rev-A or Brazilian Tec Toy cartridge.
- Android's document picker streams the selected file to staging. The native
  importer checks its size and SHA-1 before publishing a copy in the app's storage.
  Both languages remain available if the original documents move or disappear.
- Portrait places touch controls below the picture. Landscape keeps them clear
  of the central 256 game columns, including on compact displays. Widescreen
  keeps the directional pad and action buttons the same size. Their arrows,
  borders and labels come from the cartridge. Two fingers can move and act
  together. Controller input hides the buttons. The first touch shows them.
- The app keeps the shared cartridge save, options and automap in
  `Android/data/org.psmodern.game/files/shared/`. Windows can continue from
  the same files. The app flushes saves on backgrounding, pauses audio and
  returns to 1x on resume. Returning through the launcher reuses the active
  game window. Android Back cancels a binding capture.

## Verification

- The maintainer played in both orientations, with Widescreen off and on,
  used multitouch, checked rotation and audio resume, then saved and
  continued after restarting.
- The real Android picker rejected a ZIP archive and an invalid cartridge,
  imported both supported ROMs, and started the game after the disposable
  source files were removed. Foreground checks exercised encounter and reward
  rates, language switching, the Automap option and the dungeon map.
- The S24 saved after the first US dungeon walk. Windows loaded the save and
  map, completed the second walk and saved. The S24 loaded the new progress.
  The installed app's Continue screen and automap showed the returned game.
  Backup and locking checks also passed.
- On m16.4 the dungeon advanced at 59.90, 119.85 and 239.72 game frames per
  second at 1x, 2x and 4x over 20 seconds each. Audio logged zero underruns
  and queue resets. Returning from Home kept the same activity and dungeon,
  resumed audio and reset speed from 2x to 1x. An Android CTS virtual gamepad
  on the S24 hid the touch controls, and tapping Speed once restored them
  while keeping the current speed.
- Two builds with the same signing key produced the same APK (SHA-256
  `10d1cb3e0afeed18069f1c30d458d3001aa9db0f616df7ced29f42bf5dabc516`). The APK passed signature and alignment checks. Both ABI builds
  passed build-ID and zero-Z80/oracle checks. The installed package matched the installer.
- Independent Tier A reviews approved the plan and implementation, including
  the final activity fix. The full Windows suite passed 390/390 with zero skips.
  Commit `247e218` corrected the desktop fast-forward timing test. The
  [build instructions](dependencies.md#android-family-build) and
  [controls guide](player-controls.md) cover setup and play.
