PSModern - Phantasy Star (Sega Master System), rebuilt in C++

PSModern is an unofficial, open-source C++ reimplementation of Phantasy Star
for the Sega Master System. The entire game is playable from start to finish
in pure C++ with no Z80 emulator; graphics, maps, text, and music are read from
your own cartridge dump. It is not affiliated with or endorsed by SEGA or Tec
Toy, and no game data is included.

WHAT YOU NEED

An uncompressed .sms dump of your own cartridge (unzipped) from one of these
two releases:

  US Rev-A          SHA-1 07fcf297be4f4c9d92cd3f119a7ac48467e06838
  Brazil (Tec Toy)  SHA-1 fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd

Any other file is refused with an explanation.

Optional: with your own Japanese cartridge dump
(SHA-1 c9a40ddd217c58dddcd6b5c0fe66c3a50d3e68e4) added through ADD ROM, you
can hear the Japanese release's FM music and sound effects while playing the
US or Brazilian game. It is a sound source only, not a playable version.

HOW TO START

1. Extract this folder anywhere you like.
2. Double-click psmodern.exe.
3. On first launch, pick your .sms file in the Open dialog (PSModern remembers
   it automatically).
4. Choose PLAY on the start screen. Use ADD ROM to add the other language, and
   LANGUAGE or CONTROLS to adjust your setup.

Windows SmartScreen may warn about an unknown publisher because the executable
is not digitally signed. Click "More info", then "Run anyway".

DEFAULT CONTROLS

Keyboard and the first connected controller work together. Every key and button
can be changed on the in-game Controls screen (from the start screen or
Options).

  Action               Keyboard        Controller
  Move                 Arrow keys      D-pad or left stick
  Button 1 / Button 2  Z / X           A / B (positional)
  Pause / Reset        Enter / Bksp    Start / Back
  Power cycle          F5              -
  Fast-forward         Tab             RB (right shoulder)
  Options menu         Escape          Y
  Dungeon map          M               X
  Fullscreen           Alt+Enter       - (Alt+Enter is fixed)

The start screen always responds to the default navigation keys so Controls
stays reachable. Fast-forward cycles 1x -> 2x -> 4x -> 1x while keeping music
and sound effects at normal tempo.

OPTIONS (all off by default)

Press Escape (or Y on a controller) while exploring a town, the world map, or a
dungeon. Use Left/Right to change values; closing with Z/A, X/B, or Escape/Y
saves automatically.

  Encounters    0, 25, 50, 100, or 200% of original random fights (story/fixed
                fights never change).
  EXP / Meseta  100, 200, or 400% battle rewards (still at most 1 level/fight).
  Automap       Off or On; press M (or X) in a dungeon to view explored rooms.
                Exploration is tracked even while the option is off.
  Widescreen    Off or On; widens the world map, towns, and spaceship flights
                to 344x192 (16:9). Battles, menus, and dungeons stay 256x192.
  Controls      Opens the Controls remapping screen.
  Language      Auto, English, or Portugues (requires the matching ROM).
                Applies as soon as Options closes, without restarting.
  Sound         PSG (the US and Brazilian sound, the default) or FM (the
                Japanese release's FM sound; requires the Japanese ROM).
                Switches and saves at once, the music at the same point.

100% is the original game. Recommended pace: 25% Encounters, 400% EXP / Meseta
(similar to the Nintendo Switch SEGA AGES easy mode).

SAVES & COMMAND LINE

Save through the game's own menu as on the cartridge. Files live in
%APPDATA%\PSModern\shared: one cartridge-compatible .sav shared by both
languages, small companions for options and dungeon maps, and a .bak backup
made before the first write of each session. Only one PSModern window can use
a save at a time. To forget remembered ROMs, delete %APPDATA%\PSModern\roms.txt.

Command-line launch (session overrides):

  psmodern.exe "game.sms" [--encounter-rate N] [--experience-rate N]
  psmodern.exe --version

LICENSE

GNU General Public License v3.0 or later (see LICENSE.txt and THIRD-PARTY.txt).
Source and full documentation: https://github.com/carbiecarbie/psmodern
