# Special Characters and Key Reference

This page lists the supported key names other than the ordinary `A`–`Z` and
top-row `0`–`9` names. The names are accepted by the REST API, WebSocket API,
Python client, and `client/send_keys.py` helper. Names are
case-insensitive, but the names shown here are the canonical spellings.

The firmware sends USB HID key usages, not text. The operating system receiving
the keyboard report applies its selected keyboard layout. The symbol column
therefore describes the usual US-layout result; other layouts may produce a
different character.

## Control and whitespace keys

| Key name | Meaning |
| --- | --- |
| `ENTER` | Enter/Return |
| `ESC` | Escape |
| `BACKSPACE` | Backspace |
| `TAB` | Tab |
| `SPACE` | Space |

## Punctuation and symbols

These are the unshifted key positions on a standard US keyboard.

| Symbol or key position | Key name |
| --- | --- |
| `` ` `` | `GRAVE` |
| `-` | `MINUS` |
| `=` | `EQUAL` |
| `[` | `LEFT_BRACKET` |
| `]` | `RIGHT_BRACKET` |
| `\` | `BACKSLASH` |
| ISO/non-US `#` position | `NON_US_HASH` |
| `;` | `SEMICOLON` |
| `'` | `APOSTROPHE` |
| `,` | `COMMA` |
| `.` | `DOT` |
| `/` | `SLASH` |

`NON_US_HASH` is the separate HID usage used by ISO and other non-US keyboard
layouts. Its displayed character depends on the target layout. It is not an
alias for the US-layout `#` character.

## Shifted symbols

Shifted characters are produced by holding `LEFT_SHIFT` or `RIGHT_SHIFT` at
the same time as the key position. For example, `/` is `SLASH`, while `?` is
Shift+`SLASH`.

| Character | Key combination |
| --- | --- |
| `!` | `LEFT_SHIFT` + `1` |
| `@` | `LEFT_SHIFT` + `2` |
| `#` | `LEFT_SHIFT` + `3` |
| `$` | `LEFT_SHIFT` + `4` |
| `%` | `LEFT_SHIFT` + `5` |
| `^` | `LEFT_SHIFT` + `6` |
| `&` | `LEFT_SHIFT` + `7` |
| `*` | `LEFT_SHIFT` + `8` |
| `(` | `LEFT_SHIFT` + `9` |
| `)` | `LEFT_SHIFT` + `0` |
| `_` | `LEFT_SHIFT` + `MINUS` |
| `+` | `LEFT_SHIFT` + `EQUAL` |
| `{` | `LEFT_SHIFT` + `LEFT_BRACKET` |
| `}` | `LEFT_SHIFT` + `RIGHT_BRACKET` |
| `|` | `LEFT_SHIFT` + `BACKSLASH` |
| `:` | `LEFT_SHIFT` + `SEMICOLON` |
| `"` | `LEFT_SHIFT` + `APOSTROPHE` |
| `~` | `LEFT_SHIFT` + `GRAVE` |
| `<` | `LEFT_SHIFT` + `COMMA` |
| `>` | `LEFT_SHIFT` + `DOT` |
| `?` | `LEFT_SHIFT` + `SLASH` |

Use `combo` when the modifier and the key must be held together:

```python
client.combo(["LEFT_SHIFT", "SLASH"])
```

The command-line helper sends independent `press` operations in sequence. It
is suitable for direct key names such as `SLASH`, but use the Python client's
`combo` method for shifted symbols.

## Lock, function, and system keys

| Key name | Meaning |
| --- | --- |
| `CAPS_LOCK` | Caps Lock |
| `NUM_LOCK` | Num Lock |
| `F1` through `F12` | Function keys |
| `PRINT_SCREEN` | Print Screen/Screenshot |
| `SCROLL_LOCK` | Scroll Lock |
| `PAUSE` | Pause/Break |

## Navigation and editing keys

| Key name | Meaning |
| --- | --- |
| `INSERT` | Insert |
| `HOME` | Home |
| `PAGE_UP` | Page Up |
| `DELETE` | Delete |
| `END` | End |
| `PAGE_DOWN` | Page Down |
| `LEFT` | Left Arrow |
| `RIGHT` | Right Arrow |
| `UP` | Up Arrow |
| `DOWN` | Down Arrow |

## Keypad keys

Keypad keys are separate USB HID usages from the ordinary top-row digits and
are included here for completeness.

| Key name | Meaning |
| --- | --- |
| `KEYPAD_SLASH` | Keypad `/` |
| `KEYPAD_ASTERISK` | Keypad `*` |
| `KEYPAD_MINUS` | Keypad `-` |
| `KEYPAD_PLUS` | Keypad `+` |
| `KEYPAD_ENTER` | Keypad Enter |
| `KEYPAD_DOT` | Keypad decimal point |
| `KEYPAD_0` through `KEYPAD_9` | Keypad digits |

## Modifier keys and aliases

| Canonical name | Meaning |
| --- | --- |
| `LEFT_CTRL` | Left Control |
| `LEFT_SHIFT` | Left Shift |
| `LEFT_ALT` | Left Alt/Option |
| `LEFT_GUI` | Left GUI/Command/Windows |
| `RIGHT_CTRL` | Right Control |
| `RIGHT_SHIFT` | Right Shift |
| `RIGHT_ALT` | Right Alt/Option |
| `RIGHT_GUI` | Right GUI/Command/Windows |

The following aliases all map to the corresponding left-side modifier:

| Alias | Canonical key |
| --- | --- |
| `CTRL`, `CONTROL` | `LEFT_CTRL` |
| `SHIFT` | `LEFT_SHIFT` |
| `ALT` | `LEFT_ALT` |
| `GUI`, `CMD`, `WIN`, `WINDOWS` | `LEFT_GUI` |

## Examples

Direct key names can be sent with the helper:

```bash
python client/send_keys.py --cidr 192.168.2.0/24 SLASH SPACE ENTER
```

For a modifier combination, use the Python client:

```python
from remote_hid_client import RemoteHIDClient

client = RemoteHIDClient("http://192.168.1.42", "API_TOKEN")
client.combo(["LEFT_SHIFT", "SLASH"])  # ? on a US keyboard layout
```

Characters outside the standard USB HID keyboard page, such as arbitrary
Unicode symbols and many dead-key or compose sequences, are not represented by
a single key name. Their behavior depends on the target operating system and
keyboard layout.
