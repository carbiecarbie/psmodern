# Session handoff

Last updated: 2026-11-21

Release 1.2.0 preparation (Windows zip + first official signed Android APK) is
ready in the working tree, awaiting maintainer authorization to commit, build
the clean release packages into `../releases/`, and publish `v1.2.0`.

Changes prepared in the working tree:
- `CMakeLists.txt`, `tests/launcher_tests.cpp`, `tests/version_process_tests.py`,
  `docs/images/start-screen.png`: bumped version to `1.2.0`.
- `android/build.gradle`: synced `versionName` and `versionCode` (`10200`) from
  `CMakeLists.txt`, added `signingConfigs.release` / `buildTypes.release`
  (non-debuggable), and bundled `assets/LIBCXX-LICENSE.txt` and
  `assets/THIRD-PARTY.txt` with version and source commit.
- `tools/package-android-release.ps1`: clean `git archive` Android release APK
  packager with `apksigner`, 16 KiB `zipalign`, `aapt2`, notice/forbidden-file,
  and per-ABI `llvm-readelf` / `llvm-nm` zero-Z80/oracle verification.
- `README.md`, `README.pt-BR.md`, `docs/dependencies.md`, `docs/development.md`:
  updated for official Android APK release and packaging steps.

Verification so far:
- Windows `build-rel` (`1.2.0`) compiled with zero warnings; `ctest -L fast`
  passed 182/182 (zero skips).
- End-to-end test of `tools/package-android-release.ps1` (`-AllowDirty
  -UseWorkingTree`) passed all signature, alignment, badging, notice and
  per-ABI (`arm64-v8a`, `armeabi-v7a`) checks with the new release keystore
  outside Git (`D:\Projetos\Phantasy Star\android-signing\psmodern-release.jks`,
  cert SHA-256 `30d98c6879947a5f9b45f80e892bea3c3eb56527ce85a1e715c588bdfd29edfe`).

Pending: maintainer authorization to commit "Prepare release 1.2.0", package
clean `PSModern-1.2.0-windows-x64.zip` and `PSModern-1.2.0-android-arm.apk`,
play-test if desired, and tag/publish `v1.2.0`.
