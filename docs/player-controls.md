# Controls

Every button and key PSModern uses, how to change them and where the choice
is kept. The tables show the defaults. With no `controls.txt` file the game
plays with exactly these.

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

## Language

The start screen and Options also offer Language. It changes the game and
PSModern's own screens between English and Portuguese when the corresponding
ROM is available. During play, finish the current dialogue or battle first;
the game changes language after Options closes, without restarting.
Language has its own Save/Cancel, like Controls: cancelling the parent Options
screen does not undo a language already saved. This preference is global;
all languages use the same saved games, options and automap. The start screen
has one PLAY entry; ADD ROM makes another language available.

## Opening the Controls screen

- On the **start screen**, choose CONTROLS. Closing the Controls screen puts
  you back on it.
- **During play**, open the Options window and choose its fourth row,
  CONTROLS. The game stays held behind it. Closing the Controls screen puts
  you back in the Options window.

The screen lists the 13 actions with their keyboard key and controller
button, then RESTORE DEFAULTS, SAVE and CANCEL. Only seven rows fit, so the
list scrolls and MORE ABOVE or MORE BELOW tells you there is more. The bottom
line says CONTROLLER CONNECTED or NO CONTROLLER FOUND. It shows the prompt
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
keys close their own screens (Button 2 closes the map too). The window title
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
