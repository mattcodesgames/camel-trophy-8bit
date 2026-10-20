#!/usr/bin/env python3
"""Build both games and publish them into docs/ for GitHub Pages.

    python3 scripts/build_site.py            # rebuild both games, then copy into docs/
    python3 scripts/build_site.py --no-build # only copy the existing builds

Each game is a single self-contained HTML page (emulator, ROM/disk and tape inlined).
The copies in docs/ get a page title, share metadata and a small bar linking back to
the collection page at docs/index.html.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 'https://mattcodesgames.github.io/camel-trophy-8bit/'

GAMES = [
    {
        'build': [sys.executable, os.path.join(ROOT, 'zx-spectrum', 'build.py')],
        'output': os.path.join(ROOT, 'zx-spectrum', 'dist', 'CamelTrophy.html'),
        'publish': os.path.join(ROOT, 'docs', 'spectrum', 'index.html'),
        'path': 'spectrum/',
        'title': 'Camel Trophy Video Game · ZX Spectrum (1985) · Play Online in English',
        'description': 'Play Camel Trophy Game for the ZX Spectrum 48K (Aquarius Edizioni, 1985) online for free, now translated into English: '
                       'the qualifying quiz, route selection and Land Rover driving game, running in your browser.',
        'image': 'img/zx-route-map.png',
        'game': {
            'name': 'Camel Trophy Game', 'alternateName': ['Camel Trophy Video Game', "Camel Trophy '86", 'Camel Trophy ZX Spectrum'],
            'gamePlatform': 'ZX Spectrum 48K', 'datePublished': '1985',
            'publisher': {'@type': 'Organization', 'name': 'Aquarius Edizioni'},
            'author': [{'@type': 'Person', 'name': n} for n in ('Simone Majocchi', 'Giorgio Arcangeli', 'Eugenio Ciceri', 'Bruno Molteni')],
            'inLanguage': 'en', 'genre': ['Simulation', 'Driving', 'Quiz'],
            'sameAs': 'https://spectrumcomputing.co.uk/entry/27508/ZX-Spectrum/Camel_Trophy_Game',
        },
    },
    {
        'build': ['sh', os.path.join(ROOT, 'commodore-64', 'build.sh')],
        'output': os.path.join(ROOT, 'commodore-64', 'CamelTrophy.html'),
        'publish': os.path.join(ROOT, 'docs', 'c64', 'index.html'),
        'path': 'c64/',
        'title': 'Camel Trophy Video Game · Commodore 64 (1985) · Play Online',
        'description': 'Play the Camel Trophy Video Game for the Commodore 64 (Tequila, 1985) online for free: '
                       'supply drop, jungle drive, refuelling and more, running in your browser with joystick or gamepad.',
        'image': 'img/c64-plane-drop.png',
        'game': {
            'name': 'Camel Trophy Video Game', 'alternateName': ['Camel Trophy', 'Camel Trophy C64', 'Camel Trophy Commodore 64'],
            'gamePlatform': 'Commodore 64', 'datePublished': '1985',
            'publisher': {'@type': 'Organization', 'name': 'Tequila'},
            'inLanguage': 'en', 'genre': ['Arcade', 'Driving'],
            'sameAs': 'https://www.lemon64.com/game/camel-trophy',
        },
    },
]

BACK_BAR = '''<a href="../" style="display:block;padding:8px 16px;background:#111;color:#f5c400;font:600 14px/1.4 system-ui,-apple-system,'Segoe UI',sans-serif;text-decoration:none;border-bottom:1px solid #2a2a2a">&larr; All Camel Trophy video games (ZX Spectrum &amp; C64)</a>'''


def head_tags(game):
    url = BASE + game['path']
    image = BASE + game['image']
    ld = {'@context': 'https://schema.org', '@type': 'VideoGame', 'url': url, 'image': image,
          'description': game['description'], 'playMode': 'SinglePlayer', 'isAccessibleForFree': True,
          'applicationCategory': 'Game', 'operatingSystem': 'Any (web browser)', **game['game']}
    tags = [
        '<title>%s</title>' % game['title'],
        '<meta name="description" content="%s">' % game['description'],
        '<link rel="canonical" href="%s">' % url,
        '<meta property="og:type" content="website">',
        '<meta property="og:site_name" content="Camel Trophy video games">',
        '<meta property="og:url" content="%s">' % url,
        '<meta property="og:title" content="%s">' % game['title'],
        '<meta property="og:description" content="%s">' % game['description'],
        '<meta property="og:image" content="%s">' % image,
        '<meta name="twitter:card" content="summary_large_image">',
        '<meta name="twitter:title" content="%s">' % game['title'],
        '<meta name="twitter:description" content="%s">' % game['description'],
        '<meta name="twitter:image" content="%s">' % image,
        '<script type="application/ld+json">%s</script>' % json.dumps(ld),
    ]
    return '\n'.join(tags) + '\n'


def publish(game):
    html = open(game['output'], encoding='utf-8').read()
    html, n = re.subn(r'<title>.*?</title>\n?', lambda _: head_tags(game), html, count=1)
    assert n == 1, 'no <title> in ' + game['output']
    html, n = re.subn(r'(<body[^>]*>)', lambda m: m.group(1) + '\n' + BACK_BAR, html, count=1)
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
