# ZMK config for beekeeb Toucan2 Keyboard

[The beekeeb Toucan2 Keyboard](https://beekeeb.com/introducing-toucan2/) is a wireless split 42-key column‑stagger keyboard that has a display and a trackpad, with an aggressive stagger on the pinky columns.

# Keymap

![keymap](docs/keymap.svg)

Four layers, all reached from the thumbs:

| Layer | How to reach it | What it holds |
| --- | --- | --- |
| BASE | — | Letters, home row mods |
| NAV | Hold left `Space` or right `Backspace` | Digits, brackets, arrows, `Home`/`End`/`PgUp`/`PgDn` |
| FUN | Hold left `Esc` or right `Enter` | `F1`–`F12`, Bluetooth profiles on the left bottom row |
| MOU | Automatic mouse layer — active while a finger rests on the trackpad | Thumbs become mouse buttons |

Details that the picture cannot show:

- **Home row mods** sit on `A`/`S`/`D`/`F` and `J`/`K`/`L`/`;` — Cmd, Alt, Ctrl, Shift, mirrored. Cmd and Ctrl are swapped relative to their usual places on purpose. They are timerless: `tapping-term-ms` is 2000 ms and the hold is decided purely by *which* key comes next. `hold-trigger-key-positions` only lists the opposite hand plus the thumbs, so same-hand rolls always tap. `require-prior-idle-ms = 150` keeps fast typing from turning into modifiers.
- The **NAV layer carries the same home row mods** on the same physical keys.

The picture is generated from `config/toucan.keymap`, so regenerate it after every keymap change:

```bash
./scripts/draw-keymap.sh    # writes docs/keymap.svg
```

It uses [keymap-drawer](https://github.com/caksoylar/keymap-drawer) through `uvx`, with the label overrides in [keymap_drawer.config.yaml](keymap_drawer.config.yaml) and the physical layout read straight out of the shield's `default_layout`.

# License

The code in this repo is available under the MIT license.

The included shield nice_view_gem is modified from https://github.com/M165437/nice-view-gem licensed under the MIT License.

The linked trackpad module is based on https://github.com/geeksville/zmk_driver_azoteq

ZMK code snippets are taken from the ZMK documentation under the MIT license.

The embedded font QuinqueFive is designed by GGBotNet, licensed under under the SIL Open Font License, Version 1.1.
