# Player guide & controls

Controls, in-game options, automap, widescreen, fast-forward, saves, and
command-line flags. The tables below show the defaults; with no `controls.txt`
file the game plays with exactly these.

## Default controls

| Action | On the console | Keyboard | Game controller |
| --- | --- | --- | --- |
| Up, Down, Left, Right | Directional pad | Arrow keys | D-pad (the left stick always moves too) |
| Button 1 | Button 1 | Z | A |
| Button 2 | Button 2 | X | B |
| Pause | Pause button | Enter | Start |
| Reset | Reset button | Backspace | Back |
| Speed | - | Tab | Right shoulder (RB) |
| Options | - | Escape | Y |
| Map | - | M | X |
| Power cycle | Power off and on | F5 | not set |
| Fullscreen | - | Alt+Enter | not set |

That is 13 actions: the eight console inputs and five host shortcuts.

- **Speed** goes to the next speed, 1x, 2x, 4x, then back to 1x.
- **Options** opens the Options window while you walk around a town, the world
  map or a dungeon, and closes it again.
- **Map** shows the dungeon map when the Automap option is on.
- **Power cycle** switches the console off and on with your saved games kept,
  and goes back to 1x. It does nothing while an Options or Controls screen is
  open.
- Button 1 and Button 2 are the console's two buttons, and Pause is its Pause
  button.

Controller button names are positional: A is the bottom button of the four on
the right, B the right one, X the left one and Y the top one, whatever is
printed on your pad. On a PlayStation pad A is the cross; on a Nintendo-style
pad it is the button printed B. LB and RB are the shoulder buttons, LT and RT
the triggers, LSTICK and RSTICK the sticks pressed in. The first controller
found is used, including one plugged in while the game runs. Keyboard and
controller work together.

## Android touch controls

Slide your thumb across the cross to change direction. Hold a direction with
one finger and press **1** or **2** with another. In portrait the controls sit
below the picture. In landscape the cross and buttons stay in the same place
when you turn Widescreen on.

During play, **PAUSE** presses the console's Pause button, **OPT** opens
Options during exploration and the speed button cycles through 1x, 2x and 4x.
With Automap enabled, **MAP** opens the map while exploring a dungeon.
Android Back cancels in menus and opens Options during exploration.

PSModern hides Android's status and navigation bars; swipe in from the edge
to see them for a moment. The picture and the touch controls still keep clear
of a camera cutout.

A controller input hides the touch controls. If a controller is already
connected when PSModern starts, they start hidden, and they stay as they were
when you go from the start screen into the game. A touch brings them back
(that touch presses nothing), but only when no controller button is held, no
stick is pushed and the controller has been left alone for two seconds.

## What cannot be changed

- **Alt+Enter** always switches between window and fullscreen. The Fullscreen
  row of the Controls screen can add a controller button for it, but never
  changes or removes Alt+Enter. Enter, if you give it to another action,
  never acts while Alt is held.
- **The left stick** always moves, in the game and in the menus.
- **The Controls screen's own keys** (below) never follow your choices, so you
  cannot lock yourself out.
- The six keys of the directions and of Button 1 and Button 2 cannot be left
  empty. Every other key and every controller button can.

## Fast-forward

Each press of Speed (`Tab` or `RB` by default) cycles `1x`, `2x`, `4x`, then
back to `1x`. An on-screen indicator appears while accelerated. Music and sound
effects keep their normal tempo and pitch. Every session starts at `1x`, and
power cycle (`F5`) resets to `1x`. While Options is open or the window loses
focus, gameplay temporarily runs at `1x` and returns to your chosen speed
afterward.

At `2x` and `4x` the game keeps step with the screen, so on a 60 Hz screen
every picture moves the game on by the same number of frames. To do so it may
run up to 0.2% faster or slower than exactly two or four times the console's
speed. `1x` is unchanged: it always runs at the console's own rate.

## In-game options

Press Options (`Escape` or controller `Y` by default) while walking freely in a
town, on the world map, or in a dungeon. All seven rows fit on one page in the
game's own window style: the list on the left with each active value in grey,
the choices for the selected row beside it (current choice in yellow), and a
description at the bottom whose last line shows the save state (`SAVES ON
EXIT`, waiting to take effect, or save failed; `SAVES WHEN CHANGED` on
Language and Sound). Up/Down selects a row and
Left/Right changes its value; leaving with `Z`/`A`, `X`/`B`, or `Escape`/`Y`
saves automatically. Recording mode disables Options. The scene stays behind
the windows and the music keeps playing.

| Option | Values | What it does |
| --- | --- | --- |
| Encounters | 0, 25, 50, 100, 200% | Frequency of random fights. `0%` turns them off. Story and fixed fights never change. |
| EXP / Meseta | 100, 200, 400% | Experience and meseta awarded per fight. Characters still gain at most one level per fight. |
| Automap | Off, On (`NAO`, `SIM`) | Enables the dungeon floor map (`M` / `X`). Off is the original game. |
| Widescreen | Off, On (`NAO`, `SIM`) | Widens the world map, towns, and spaceship flights to 16:9. Off is the original game. |
| Controls | - | Button 1 opens the Controls screen to remap keys and buttons. |
| Language | Auto, English, Portugues | Language of the game text and PSModern's own screens. Saves and switches UI immediately; game text updates when Options closes. Each language needs its ROM; Auto follows the ROM you play. |
| Sound (`SOM`) | PSG, FM | PSG is the sound of the US and Brazilian cartridges and the original; FM is the Japanese cartridge's FM sound. Saves and switches immediately, the music at the same point. FM needs the Japanese ROM ([Sound](#sound)). |

`100%` is the original game. Setting **Encounters 25%** and **EXP / Meseta
400%** gives a pace similar to the Nintendo Switch SEGA AGES easy mode. Rate
changes apply once the current action ends, so a battle in progress finishes
under the old rates; the window title shows active rates and any pending
change. Settings are stored for the shared save file across all five save slots.

## Automap

With Automap on, press Map (`M` or controller `X`) while walking in a dungeon
to view the current floor. The header shows the floor number (`FLOOR 1` /
`ANDAR 1` is the lowest floor with an exit to the outside, `FLOOR 2` above it,
`FLOOR B1` below it). `M`, `X`, `Escape`, or `B` closes the map while the game
waits and music keeps playing.

The map shows only explored areas: corridors, walls, doors (plain, locked, and
magic), stairs, exits to the outside, and chests (opened or closed). A pitfall
appears once you fall through it and a hidden passage once you walk through it;
bosses and guardians are never marked. Maps are saved per game slot so Continue
restores that slot's map and a new game starts empty. Exploration is tracked
even while Automap is off, so turning it on later shows everything you have
already seen.

## Language

The start screen and Options also offer Language. It changes the game and
PSModern's own screens between English and Portuguese when the corresponding
ROM is available. On the start screen, LANGUAGE opens a Language screen with
its own Save/Cancel, like Controls. In Options, LANGUAGE is the sixth row:
left and right step through AUTO, ENGLISH and PORTUGUES (each language named
in its own words, a language without its ROM skipped and shown dark grey),
and each step is saved at once and switches PSModern's screens at once.
During play, finish the current dialogue or battle first; the game's own text
changes language after Options closes, without restarting. If the choice
cannot be saved, the previous one comes back and the bottom line says SAVE
FAILED - RETRY; press Left or Right again to retry. Leaving Options saves its
displayed rates, automap and widescreen settings, but does not retry a
language or sound choice.
This preference is global; all languages use the same saved games, options
and automap. The start screen
has one PLAY entry; ADD ROM makes another language available.

## Sound

For the Japanese FM music and effects, add your Japanese cartridge dump
with **ADD ROM** on the start screen. Keep a US Rev-A or Brazilian cartridge
registered to play. On Android the file picker keeps a copy in app storage.
On Windows the application remembers the selected path in `sound-rom.txt`.

Choose PSG or FM on the SOUND row of Options. The change is heard at once,
with the music at the same point, and saved at once. If it cannot be saved,
the previous sound stays and the bottom line says SAVE FAILED - RETRY; press
Left or Right again to retry. Without the Japanese
ROM the row stays on PSG and says that FM needs it; if the FM sound stopped
with an error, the row says FM is off until the next power cycle.

The default sound is PSG. The installation's choice lives in `sound.txt`
beside `language.txt`. Its first line is `psmodern-sound 1` and its second
line is `psg` or `fm`. If the Japanese image becomes unavailable the game
plays PSG and keeps the preference for the next session. Recording uses the
playable cartridge's original audio. Both sound modes follow normal musical
time during fast-forward.

## Options: widescreen

The Options screen has a WIDESCREEN row (the same word in Portuguese), the
fourth after Encounters, EXP / Meseta and Automap. Left or right switches it
OFF or ON (NAO or SIM in Portuguese); it is OFF by default. Like Automap it takes effect when you leave
Options, with no restart, and is saved with your other options.

- **Where it applies.** With WIDESCREEN ON the world map, the towns and the
  spaceship flight between planets are
  shown 344 by 192 pixels, 16:9: the original 256 pixels in the middle and 44
  more on each side, drawn from the map (in space, from the stars) with the game's own tiles
  and colours, including the 8 pixels on the left that the cartridge leaves blank. Walking
  past the edge of a town shows what the game has there; the world maps wrap
  around. In space the sides repeat the stars: those at the right of the
  middle are the same as those at the left. Battles, dialogue, shops,
  houses, dungeons and the other screens keep their 256 pixels, centred between black bars. Fades and
  the moments around a transfer between maps may show black bars for a moment.
  Menus and screens of PSModern itself (Options, Controls, Language, the
  dungeon map, the speed mark) stay 256 pixels wide in the middle. The start
  screen is always 256 pixels wide.
- **The window.** While WIDESCREEN is ON the window is as wide as the wide
  picture for the whole session, so it never changes size during play.
  Switching the option resizes a normal window to the wide or the narrow
  shape at the scale it has, shrinking it by whole steps if it would not fit on
  the screen; a maximized or fullscreen window keeps its size and only the
  picture changes. At 1366 by 768 fullscreen the wide picture is drawn at 3
  times its size instead of 4.
- **Recording.** While recording, the Options shortcut is ignored, so the
  row cannot be changed: the game stays exactly as the cartridge ran it.
- **The game never notices.** WIDESCREEN only changes what is shown. With it ON
  the game runs exactly as with it OFF; see
  [how widescreen reads the map](how-it-works/world-map.md#how-widescreen-reads-this).

## Opening the Controls screen

- On the **start screen**, choose CONTROLS. Closing the Controls screen puts
  you back on it.
- **During play**, open the Options window and choose its fifth row,
  CONTROLS. The game stays held behind it. Closing the Controls screen puts
  you back in the Options window.

The screen lists the 13 actions with their keyboard key and controller
button, then RESTORE DEFAULTS, SAVE and CANCEL. Only seven rows fit, so the
list scrolls and MORE ABOVE or MORE BELOW tells you there is more. A small
window at the bottom says CONTROLLER CONNECTED or NO CONTROLLER FOUND. It shows the prompt
while an entry waits for an answer and, for two seconds, the result of what
you just did (SWAPPED WITH, CLEARED and the like). DEFAULTS - NOT SAVED YET and
SAVE FAILED - RETRY stay until your next press.

### Moving around

These keys always work here, whatever you have set:

| What | Keyboard | Controller |
| --- | --- | --- |
| Move between rows, and between the keyboard and controller columns | Arrow keys | D-pad or left stick |
| Choose the highlighted cell or command | Enter or Z | A |
| Clear the highlighted cell | Backspace or Delete | X |
| Leave without saving | Escape or X | B |

Leaving without saving is the same as CANCEL.

The screen ignores a navigation input that is held when it opens (and again
after a capture or after the window regains focus) until you let go of that
one input. Any other held key or stick direction is not affected, and a
blocked input never hides another one that means the same (the arrow and the
D-pad, say), so nothing held can keep the screen from answering.

### Changing an entry

1. Move to the row of the action and to the column you want: KEYBOARD or
   GAMEPAD.
2. Press the accept key (Enter, Z or A). The cell goes blank and the bottom
   line says PRESS A KEY FOR ... or PRESS BUTTON FOR ...
3. Press the new key, or the new controller button. The capture waits only
   for the accept you pressed to be released; that press is not taken as the
   answer, and nor is anything that was already held, only a fresh press. If
   several keys go down together, the one with the lowest scancode wins.
4. The cell shows the new name. Nothing is saved yet.
5. Choose SAVE to keep your changes.

The capture gives up by itself after five seconds, when the window loses focus,
or if you press something on the other device (a controller button while
waiting for a key, or a key while waiting for a button). The entry is then left
as it was. Alt and the Windows keys are never an answer and are ignored
without a message; keys the game cannot name, such as Print Screen or the
media keys, show KEY NOT AVAILABLE. In both cases the screen keeps waiting.
Escape can be chosen as an answer, so to give up wait the five seconds or
press the other device. The left stick cannot be chosen. A trigger counts as
pressed once it is more than halfway down and as released only when it comes
back below a quarter of its travel, so it does not flicker at the edge. It can
be chosen like a button. A trigger already held when the controller is
connected or the game starts does not count as a press.

### When a key is already in use

The two actions trade. If you give Button 2 the key Z, which Button 1 uses,
Button 2 gets Z and Button 1 gets Button 2's old key, X. The bottom line says
SWAPPED WITH BUTTON 1. Controller buttons trade the same way. If the entry
you are setting is empty, the other action ends up empty.

One trade is refused, with no change: giving an action that has no key (for
example Power cycle after you cleared it) a key that one of the six required
keys uses. It would leave that action without a key. The bottom line says KEY
USED BY and its name.

### Clearing an entry

Highlight a cell and press Backspace, Delete or X on a controller. The cell
shows NONE and the action cannot be triggered from that device. The six
required keys cannot be cleared and say, for example, UP NEEDS A KEY. The
fullscreen key says ALT ENTER IS FIXED, for clearing and for setting.

### RESTORE DEFAULTS, SAVE and CANCEL

- **RESTORE DEFAULTS** puts the defaults in the list. It says DEFAULTS - NOT
  SAVED YET, because nothing is written until you choose SAVE.
- **SAVE** writes `controls.txt` and starts using the new controls at once,
  with no restart. The screen closes. If the file cannot be written it says SAVE
  FAILED - RETRY and stays open.
- **CANCEL** closes the screen and throws the changes away.

### Menus follow your choices

The Options window and the dungeon map use your controls: the four direction
inputs move, Button 1 chooses and Button 2 goes back, and the Options and Map
keys close their own screens (Button 2 closes the map too). Leaving Options
automatically saves its displayed rates, Automap and Widescreen settings. The window title
lists the current keys for Speed and Options, and for Map while the Automap
option is on, for example `Speed 1X (TAB / RB)`; an action with no key or
button set is left out.

The **start screen** is the exception. It ignores `controls.txt` for moving
and choosing, so CONTROLS is always within reach: arrows, D-pad or left stick
move, Z, Enter or controller A or Start choose, and Escape quits. Only the
controller button you gave to Fullscreen, if any, also works there.

## The controls file

`controls.txt` is in `%APPDATA%\PSModern`, next to `roms.txt`. Nothing is
written until you choose SAVE, so a player who never opens the Controls screen
has no file. Delete the file to go back to the defaults.

It is a short text file you can read and edit with Notepad. The first line
names the format and every other line is `<action> key <name>` or
`<action> pad <name>`. What PSModern writes begins like this:

```
psmodern-controls 1
# PSModern controls. Change them on the CONTROLS screen, or edit this file.
# Each line: <action> key|pad <name>, or NONE to leave it unset.
# The left stick always moves; Alt+Enter always toggles fullscreen.
up key UP
up pad DPUP
down key DOWN
down pad DPDOWN
...
button1 key Z
button1 pad A
...
power key F5
power pad NONE
fullscreen pad NONE
```

- The actions are `up down left right button1 button2 pause reset speed
  options map power fullscreen`. Fullscreen has only a `pad` line.
- Key names are the ones the Controls screen shows: letters A to Z, digits 0
  to 9, F1 to F12, ENTER, ESCAPE, BACKSPACE, TAB, SPACE, arrows (UP, DOWN, LEFT,
  RIGHT), INSERT, DELETE, HOME, END, PAGEUP, PAGEDOWN, CAPSLOCK, LCTRL, RCTRL,
  LSHIFT, RSHIFT, MINUS, EQUALS, LBRACKET, RBRACKET, BACKSLASH, SEMICOLON, QUOTE,
  GRAVE, COMMA, PERIOD, SLASH, and the number pad (KP0 to KP9, KPENTER, KPPLUS,
  KPMINUS, KPSTAR, KPSLASH, KPPERIOD), and the two extra keys of some
keyboards, OEM102 and INTL1.
- Controller names are A, B, X, Y, BACK, GUIDE, START, LEFTSTICK, RIGHTSTICK,
  LEFTSHOULDER, RIGHTSHOULDER, DPUP, DPDOWN, DPLEFT, DPRIGHT, LEFTTRIGGER,
  RIGHTTRIGGER, and for pads that have them MISC1, PADDLE1 to PADDLE4 and
  TOUCHPAD. The Controls screen shows shorter names (LB, RB, D UP, LSTICK).
- `NONE` leaves an action without a key or button. It cannot be used for the
  six required keys.
- Capitals and small letters are the same, blank lines and lines starting with
  `#` are ignored, and Notepad's own line endings and byte-order mark are fine.

**A damaged file never stops the game.** A line PSModern cannot use (a
misspelled name, a missing word, `NONE` for a required key, a line for the
fullscreen key) is skipped and the rest still applies; anything not mentioned
keeps its default. A file that is not from PSModern (wrong first line), larger
than 4096 bytes or unreadable is ignored altogether and the defaults are used.
The game starts as usual in every case. It only notes the problem on the
console it was started from, and nothing is shown in the window. The next SAVE
writes a fresh file. If a hand-edited file gives the same key to two actions,
the second one traded with the first as on the Controls screen, so check the
result on the screen.

The keys you choose are turned into the console's buttons before the game
reads them; see [how it works: reading the controller](how-it-works/controls.md)
for what the game does next, and [development](development.md#input) for the
code.

## Saves

Save through the game's own menu, just as on the cartridge. Files are stored in
`%APPDATA%\PSModern\shared` on Windows (`Android/data/org.psmodern.game/files/shared/`
on Android) and shared across both languages:

- `phantasy-star.sav` — the raw 8 KiB cartridge save, readable by other Master
  System emulators;
- a small companion file storing your options;
- a companion file storing the dungeon maps for your saved games;
- a `.bak` backup of the previous save made before the first write of each
  session.

On first launch, PSModern preserves legacy per-version US/BR saves and imports
them once (asking which to continue if both differ, leaving both originals
untouched). Adding or removing a ROM never deletes your saves. Global
`roms.txt`, `language.txt`, and `controls.txt` stay in the root PSModern
folder; delete `roms.txt` to make PSModern forget your remembered ROMs.

Copying only the raw `.sav` to another emulator carries your progress without
the options; a raw save without its companion file runs with original rules.
Returning rates to `100%` keeps all experience and meseta already earned. Only
one running copy of PSModern can use a save at a time.

## Command line

`psmodern.exe` accepts a ROM path and session-only rate flags:

```sh
psmodern.exe "path/to/your-game.sms" --encounter-rate 25 --experience-rate 400
psmodern.exe --version
```

- `--encounter-rate` accepts `0`, `25`, `50`, `100`, or `200`.
- `--experience-rate` accepts `100`, `200`, or `400`.

Omitted flags use your saved preferences, and an explicit `100` overrides a
saved rate for that session. Opening and closing the Options window saves the
currently displayed values, including any launch flags. The developer flag
`--record` enforces original rules and refuses any non-100 rate (see
[development](development.md#record-mode)).
