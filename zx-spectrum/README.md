# Camel Trophy — ZX Spectrum in WebAssembly

A playable browser version of *Camel Trophy* (Aquarius Edizioni / Digiteam), translated
into English and run on a ZX Spectrum 48K emulator written in C and compiled to WebAssembly.

The original Italian tape (`tapes/CamelTrophy.tzx`) is the source; the build translates it
into `tapes/CamelTrophy_EN.tzx`, and only the English tape is embedded in the page.

## Play

Open **`dist/CamelTrophy.html`** in any modern browser. It is a single self-contained
file (WASM, ROM and tape are inlined), so it works straight from disk or from any static host.

Click the screen to start: the page types `LOAD ""` and plays the tape (turbo loading is on).
The tape stops after each part; when the game asks for the next part, playback resumes
automatically. The **Load** buttons reset and jump straight to a part.

| Part | Contents |
| --- | --- |
| 1 · Qualifying quiz | Multiple-choice questions (number key, `Enter` to confirm), name entry, crew code |
| 2 · Route selection | Zaire / Amazon / Borneo / D.I.Y. routes and stage briefings; `Y` accepts a route |
| 3 · Driving game | Choose `1` keyboard (`Q W E R T`) or `2` Kempston (arrow keys + `Tab`) |

Keys: PC keyboard maps to the Spectrum keyboard (`Shift` = Caps Shift, `Ctrl`/`Alt` = Symbol
Shift, `Backspace` = Delete). Arrow keys act as a joystick (Kempston, Cursor, Sinclair, QAOP).
An on-screen Spectrum keyboard is available (shown by default on touch devices).
**Save / Restore** keep one snapshot in the browser.

## Build

```bash
pip install ziglang        # provides a wasm32 C compiler + linker
python3 build.py
```

`build.py` compiles `src/spectrum.c` → `web/spectrum.wasm`, translates the tape and writes
`dist/CamelTrophy.html`.
Set `ZIG=/path/to/zig` or `CC_WASM="clang --target=wasm32 ..."` to use another toolchain.
For development, `web/index.html` + `web/app.js` also run unbundled from a local web server
(`python3 -m http.server` from the project root, then open `/web/`).

## Layout

- `src/z80.c` — Z80 CPU core (all documented + undocumented opcodes and flags)
- `src/spectrum.c` — 48K machine: memory, ULA video with per-line border, beeper audio,
  keyboard matrix, Kempston joystick, TZX/TAP decoder with edge-accurate tape playback
  (custom turbo loaders work unmodified), snapshots
- `tools/translate.py` — builds the English tape: rewrites the BASIC programs of parts 1–2
  (re-linking system variables), patches part 3's machine-code text in place at the same
  length, redraws picture captions and re-encodes the run-length-compressed route maps
- `tools/translation_en.py` — all the English text, in one table keyed by the Italian original
- `web/` — HTML/JS frontend (canvas, AudioWorklet, input, tape controls)
- `test/headless.c`, `test/probe.c` — native test harnesses that drive the same C core
  without a browser and dump frames (`test/topng.py` converts them to PNG)
- `roms/48.rom` — Sinclair 48K ROM (© Amstrad, who allow it to be distributed with emulators)
