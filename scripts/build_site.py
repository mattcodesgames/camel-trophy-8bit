#!/usr/bin/env python3
"""Build both games and publish them into docs/ for GitHub Pages.

    python3 scripts/build_site.py            # rebuild both games, then copy into docs/
    python3 scripts/build_site.py --no-build # only copy the existing builds

Each game is a single self-contained HTML page (emulator, ROM/disk and tape inlined).
The copies in docs/ get a page title, share metadata and a small bar linking back to
the collection page at docs/index.html.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

GAMES = [
    {
        'build': [sys.executable, os.path.join(ROOT, 'zx-spectrum', 'build.py')],
        'output': os.path.join(ROOT, 'zx-spectrum', 'dist', 'CamelTrophy.html'),
        'publish': os.path.join(ROOT, 'docs', 'spectrum', 'index.html'),
        'title': 'Camel Trophy · ZX Spectrum',
        'description': 'Play Camel Trophy (Aquarius Edizioni, 1985) for the ZX Spectrum 48K in your browser, translated into English.',
        'image': 'img/zx-route-map.png',
    },
    {
        'build': ['sh', os.path.join(ROOT, 'commodore-64', 'build.sh')],
        'output': os.path.join(ROOT, 'commodore-64', 'CamelTrophy.html'),
        'publish': os.path.join(ROOT, 'docs', 'c64', 'index.html'),
        'title': 'Camel Trophy · Commodore 64',
        'description': 'Play Camel Trophy (Tequila, 1985) for the Commodore 64 in your browser.',
        'image': 'img/c64-plane-drop.png',
    },
]

BACK_BAR = '''<a href="../" style="display:block;padding:8px 16px;background:#111;color:#f5c400;font:600 14px/1.4 system-ui,-apple-system,'Segoe UI',sans-serif;text-decoration:none;border-bottom:1px solid #2a2a2a">&larr; Camel Trophy &middot; 8-bit collection</a>'''


def publish(game):
    html = open(game['output'], encoding='utf-8').read()
    head = ('<title>%s</title>\n<meta name="description" content="%s">\n'
            '<meta property="og:title" content="%s">\n<meta property="og:description" content="%s">\n'
            '<meta property="og:image" content="../%s">\n') % (
        game['title'], game['description'], game['title'], game['description'], game['image'])
    html, n = re.subn(r'<title>.*?</title>\n?', head, html, count=1)
    assert n == 1, 'no <title> in ' + game['output']
    html, n = re.subn(r'(<body[^>]*>)', r'\1\n' + BACK_BAR.replace('\\', '\\\\'), html, count=1)
    assert n == 1, 'no <body> in ' + game['output']
    os.makedirs(os.path.dirname(game['publish']), exist_ok=True)
    with open(game['publish'], 'w', encoding='utf-8') as f:
        f.write(html)
    print('published', os.path.relpath(game['publish'], ROOT), '(%d KB)' % (len(html) // 1024))


def main():
    build = '--no-build' not in sys.argv
    for game in GAMES:
        if build:
            subprocess.check_call(game['build'], cwd=os.path.dirname(game['build'][-1]))
        publish(game)


if __name__ == '__main__':
    main()
