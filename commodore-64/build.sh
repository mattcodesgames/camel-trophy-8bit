#!/bin/sh
# Build web/c64.wasm from src/c64.c using the zig toolchain (clang + wasm-ld),
# then produce CamelTrophy.html: a single self-contained page (wasm + disk inlined)
# that runs straight from the file system.
# One-time setup:  python3 -m venv .venv && .venv/bin/pip install ziglang
set -e
cd "$(dirname "$0")"
python3 tools/mkfont.py
# new title/credits intro in place of the CFB-Soft one: assets/CamelTrophy-original.d64 -> web/camel.d64
python3 tools/mkintro.py
.venv/bin/python -m ziglang cc src/c64.c -o web/c64.wasm \
  -target wasm32-freestanding -O2 -mbulk-memory -nostdlib -fno-builtin-memset -s \
  -Wl,--no-entry -rdynamic
python3 - <<'PY'
import base64
html = open('web/index.html').read()
wasm = base64.b64encode(open('web/c64.wasm', 'rb').read()).decode()
disk = base64.b64encode(open('web/camel.d64', 'rb').read()).decode()
html = html.replace('const EMBEDDED_WASM = null;', 'const EMBEDDED_WASM = "%s";' % wasm, 1)
html = html.replace('const EMBEDDED_DISK = null;', 'const EMBEDDED_DISK = "%s";' % disk, 1)
open('CamelTrophy.html', 'w').write(html)
PY
ls -l web/c64.wasm CamelTrophy.html
