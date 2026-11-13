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
4. Choose PLAY on the start screen. ADD ROM adds the other language. LANGUAGE
   selects English or Portuguese; each language needs its corresponding ROM.
   The screen also offers CONTROLS, OPEN SAVES FOLDER and QUIT.

Windows SmartScreen may warn about an unknown publisher because the program is
not digitally signed. Click "More info", then "Run anyway".

CONTROLS

These are the defaults. Every key and button can be changed on the Controls
screen: choose CONTROLS on the start screen, or on the Options window while
playing.

  Console              Keyboard        Game controller
  Directional pad      Arrow keys      D-pad or left stick
  Button 1 / Button 2  Z / X           A / B
  Pause                Enter           Start
  Reset                Backspace       Back
  Power off and on     F5              not set
  Speed 1x / 2x / 4x   Tab             Right shoulder (R / RB)
  Options window       Escape          Y
  Dungeon map          M               X
  Fullscreen           Alt+Enter       not set (Alt+Enter is fixed)

Keyboard and controller work together. The first controller found is used.
The start screen always uses the defaults (arrows, Z or Enter, Escape; D-pad,
A or Start), so CONTROLS stays within reach. Controller buttons are named by
position: A is the bottom button, whatever is printed on your pad. Each press
of Tab or RB cycles the speed 1x, 2x, 4x, then 1x again; music keeps its
normal tempo. The picture is the original 256x192, scaled in whole steps in a
resizable window.

OPTIONS (off by default)

Press Escape (or Y on a controller, or the buttons you set) while exploring a
town, the world map or a dungeon. Left/Right changes a value. Leaving with
Z/A, X/B or Escape/Y automatically saves the displayed values.

  Encounters   0, 25, 50, 100 or 200 percent of the original random fights.
  Experience   100, 200 or 400 percent; battle meseta follows it.
  Automap      Off or On; a map of the current dungeon floor (off by default).
  Controls     Opens the Controls screen.
  Language     Changes the language during exploration after saving your choice.

100 is the original game. Recommended: encounters 25, experience 400.
Story and fixed fights never change.

With Automap on, press M or controller X while walking in a dungeon to see the
current floor, labelled at the top FLOOR 1, 2... (FLOOR 1 is the lowest floor
with an exit outside) or FLOOR B1... below it. The game pauses and music
continues. The map shows explored places, and is saved with each game.
Exploration is recorded even while the option is off.

CHANGING THE CONTROLS

On the Controls screen move with the arrow keys (or D-pad), press Enter, Z or A
on a cell, then press the new key or button. A key already in use trades places
with the entry you change. Backspace, Delete or X clears a cell; Escape, X or B
leaves without saving. Choose RESTORE DEFAULTS, SAVE or CANCEL at the bottom.
SAVE writes controls.txt in %APPDATA%\PSModern, next to roms.txt; delete it to
return to the defaults. A damaged file never stops the game: lines it cannot
use are skipped and the defaults fill the gaps. The full list is in
docs/player-controls.md of the source.

SAVES

The game saves through its own menu, as on the cartridge. Files are kept in
%APPDATA%\PSModern\shared: one cartridge-compatible raw save shared by both
languages, a companion settings file, a dungeon map file, and a .bak copy of
the previous save made before the first write of each session. Only one copy
of PSModern may use a save at a time.

On first use, old US and Brazilian saves are preserved and imported. If both
sets differ, choose which one to continue. New progress is saved to the shared
folder and is not copied back to old per-version files.

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
