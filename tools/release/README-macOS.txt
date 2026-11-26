PSModern - Phantasy Star (Sega Master System), rebuilt in C++ - macOS

PSModern is an unofficial, open-source C++ reimplementation of Phantasy Star
for the Sega Master System. Graphics, maps, text, and music are read from your
own cartridge dump. It is not affiliated with or endorsed by SEGA or Tec Toy,
and no game data is included.

This is a universal app: it runs natively on Apple silicon (M1 and later) and
on Intel Macs, macOS 11 Big Sur or newer.

WHAT YOU NEED

An uncompressed .sms dump of your own cartridge (unzipped):

  US Rev-A          SHA-1 07fcf297be4f4c9d92cd3f119a7ac48467e06838
  Brazil (Tec Toy)  SHA-1 fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd

Optional: your own Japanese cartridge dump
(SHA-1 c9a40ddd217c58dddcd6b5c0fe66c3a50d3e68e4), added through ADD ROM,
provides the Japanese FM music and sound effects.

HOW TO START

1. Drag PSModern.app to your Applications folder (or anywhere you like).
2. The app is not notarized by Apple, so the first launch is blocked.
   Right-click (or Control-click) PSModern.app, choose Open, then Open again.
   On macOS 15 or newer, if there is no Open button: try to open it once,
   then go to System Settings > Privacy & Security and click "Open Anyway".
   Alternatively, in Terminal:
     xattr -dr com.apple.quarantine /Applications/PSModern.app
3. On first launch, pick your .sms file in the Open dialog (PSModern remembers
   it automatically).
4. Choose PLAY on the start screen.

DEFAULT CONTROLS

Keyboard and the first connected controller work together. Every key and button
can be changed on the in-game Controls screen.

  Action               Keyboard        Controller
  Move                 Arrow keys      D-pad or left stick
  Button 1 / Button 2  Z / X           A / B (positional)
  Pause / Reset        Return / Delete Start / Back
  Power cycle          F5 (fn+F5)      -
  Fast-forward         Tab             RB (right shoulder)
  Options menu         Escape          Y
  Dungeon map          M               X
  Fullscreen           Option+Return   -

SAVES

Saves, settings and remembered ROMs live in
  ~/Library/Application Support/PSModern/
The files inside shared/ are the same format as on Windows
(%APPDATA%\PSModern\shared) and Android, so you can copy them between devices
while PSModern is closed.

COMMAND LINE

The executable inside the bundle takes the same flags as on Windows, e.g.:
  /Applications/PSModern.app/Contents/MacOS/PSModern "game.sms" --encounter-rate 25 --experience-rate 400

LICENSE

GPL-3.0-or-later (LICENSE.txt). Third-party notices: THIRD-PARTY.txt.
