#!/usr/bin/env python3
"""Builds the two copies of the app from web/app.src.html:
   docs/index.html          full page for GitHub Pages (GPS works here)
   web/heltec-heatmap.html  fragment for the Claude artifact preview
Run from the repo root:  python3 web/build.py"""
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent
src = (root / 'web/app.src.html').read_text()
css = (root / 'web/vendor/leaflet-1.9.4.css').read_text()
frag = src.replace('/*LEAFLET_CSS*/', css)
(root / 'web/heltec-heatmap.html').write_text(frag)
head, body = frag.split('<header class="top">', 1)
page = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">\n'
        + head +
        '<style>:root{padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}'
        'body{margin:0}[hidden]{display:none!important}</style>\n</head>\n<body>\n<header class="top">'
        + body + '\n</body>\n</html>\n')
(root / 'docs/index.html').write_text(page)
print('built docs/index.html and web/heltec-heatmap.html')
