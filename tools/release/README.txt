PSModern - Phantasy Star for the Sega Master System, rebuilt in C++

PSModern is an unofficial, free fan reimplementation of Phantasy Star for the
Sega Master System. It runs the original game from your own cartridge dump and
replaces its routines with C++ one at a time. It is not affiliated with, or
endorsed by, SEGA or Tec Toy. No game data is included in this download.

WHAT YOU NEED

A dump of your own cartridge: an uncompressed .sms file (not zipped) of one of
these two releases:

  US Rev-A          SHA-1 07fcf297be4f4c9d92cd3f119a7ac48467e06838
  Brazil (Tec Toy)  SHA-1 fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd

Any other file is refused with a message.

HOW TO START

1. Unzip the folder anywhere you like.
2. Double-click psmodern.exe.
3. The first time, pick your .sms file in the Open dialog. PSModern remembers it.
4. A start screen offers: PLAY (US/BR), CHOOSE ANOTHER ROM, OPEN SAVES FOLDER
   and QUIT.

Windows SmartScreen may warn about an unknown publisher because the program is
not digitally signed. Click "More info", then "Run anyway".

CONTROLS

  Console              Keyboard        Game controller
  Directional pad      Arrow keys      D-pad or left stick
  Button 1 / Button 2  Z / X           A / B
  Pause                Enter           Start
  Reset                Backspace       Back
  Power off and on     F5              -
  Speed 1x / 2x / 4x   Tab             Right shoulder (R / RB)
  Options window       Escape          Y
  Fullscreen           Alt+Enter       -

Keyboard and controller work together. Each press of Tab or RB cycles the
speed 1x, 2x, 4x, then 1x again; music keeps its normal tempo. The picture is
the original 256x192, scaled in whole steps in a resizable window.

OPTIONS (off by default)

Press Escape (or Y on a controller) while exploring a town, the world map or a
dungeon. Left/Right changes a value, Z or A saves and closes, X or B discards.

  Encounters   0, 50, 100 or 200 percent of the original random fights.
  Experience   100, 200 or 400 percent; battle meseta follows it.

100 is the original game. Recommended: encounters 50, experience 400.
Story and fixed fights never change.

SAVES

The game saves through its own menu, as on the cartridge. Files are kept in
%APPDATA%\PSModern (one per version): a cartridge-compatible raw save, a small
companion settings file, and a .bak copy of the previous save made before the
first write of each session. Only one copy of PSModern may use a save at a time.

COMMAND LINE

psmodern.exe still accepts a ROM path and the same options:

  psmodern.exe "game.sms" [--encounter-rate N] [--experience-rate N]
  psmodern.exe --version

To make PSModern forget the remembered ROM, delete %APPDATA%\PSModern\roms.txt.

LICENSE

PSModern is free software under the GNU General Public License, version 3 or
later. See LICENSE.txt and THIRD-PARTY.txt (libraries, licenses and the source
this build corresponds to).

Project page and source: https://github.com/carbiecarbie/psmodern
