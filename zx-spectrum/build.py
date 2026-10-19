#!/usr/bin/env python3
"""Build the Camel Trophy WebAssembly player.

1. Compiles src/spectrum.c (which includes src/z80.c) to web/spectrum.wasm.
2. Translates the original Italian tape into tapes/CamelTrophy_EN.tzx (tools/translate.py).
3. Writes dist/CamelTrophy.html: a single self-contained page with the
   wasm, ROM and tape inlined, so it can be opened straight from disk.

Needs a C compiler that can link wasm32. By default uses Zig
(`pip install ziglang`); set ZIG=/path/to/zig or CC_WASM="clang ..." to override.
"""
import base64
import os
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def compiler():
    if os.environ.get("CC_WASM"):
        return shlex.split(os.environ["CC_WASM"])
    if os.environ.get("ZIG"):
        return [os.environ["ZIG"], "cc"]
    return [sys.executable, "-m", "ziglang", "cc"]


def build_wasm():
    out = os.path.join(ROOT, "web", "spectrum.wasm")
    cmd = compiler() + [
        "--target=wasm32-freestanding",
        "-O2",
        "-nostdlib",
        "-fvisibility=hidden",
        "-Wall",
        "-Wno-unused-function",
        "-Wl,--no-entry",
        "-Wl,--export-dynamic",
        "-Wl,--initial-memory=33554432",
        "-o", out,
        os.path.join(ROOT, "src", "spectrum.c"),
    ]
    print(" ".join(shlex.quote(c) for c in cmd))
    subprocess.check_call(cmd)
    return out


IT_TAPE = os.path.join(ROOT, "tapes", "CamelTrophy.tzx")
EN_TAPE = os.path.join(ROOT, "tapes", "CamelTrophy_EN.tzx")


def build_tape():
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "translate.py"), IT_TAPE, EN_TAPE])


def b64(path):
    with open(path, "rb") as f:
        return base64.b64encode(f.read()).decode("ascii")


def build_html(wasm):
    with open(os.path.join(ROOT, "web", "index.html"), encoding="utf-8") as f:
        html = f.read()
    with open(os.path.join(ROOT, "web", "app.js"), encoding="utf-8") as f:
        app = f.read()
    inline = (
        "<script>window.EMBEDDED = {"
        f"wasm: '{b64(wasm)}', "
        f"rom: '{b64(os.path.join(ROOT, 'roms', '48.rom'))}', "
        f"tape: '{b64(EN_TAPE)}'"
        "};</script>\n"
        f"<script>\n{app}\n</script>"
    )
    html = html.replace('<script src="app.js"></script>', inline)
    os.makedirs(os.path.join(ROOT, "dist"), exist_ok=True)
    out = os.path.join(ROOT, "dist", "CamelTrophy.html")
    with open(out, "w", encoding="utf-8") as f:
        f.write(html)
    print("wrote", out)


if __name__ == "__main__":
    build_tape()
    build_html(build_wasm())
