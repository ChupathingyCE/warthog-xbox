#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 JizzyRivers and Warthog's contributors
# Part of Warthog: under its license, with its console SDK permission (README.md, "License").
"""The SDK's file names as the code spells them, for a build on Linux (docs/building.md,
"Linux"). Linux is case sensitive and the SDK's names aren't consistent (XTL.h, StdIO.h,
sys/Types.h): symlinks beside the originals, which aren't touched, each name in lower case
and each spelling an #include in the SDK or the given source trees uses. Run once after
copying the SDK into OXDK, and again after a submodule update:

    python3 tools/xdk_case_links.py port chupathingyce/source chupathingyce/port/console chupathingyce/port/linux/game
"""
import os, re, sys
from pathlib import Path

import os
xdk = Path(os.environ.get('OXDK_DIR', Path.home() / 'OXDK')) / 'xbox' / 'xdk'
roots = [Path(a) for a in sys.argv[1:]]
INCLUDE = re.compile(rb'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
made = 0

def link(folder, name, target):
    global made
    path = folder / name
    if not path.exists() and not path.is_symlink():
        path.symlink_to(target)
        made += 1

folders = [xdk / 'include', xdk / 'lib'] + [d for d in (xdk / 'include').rglob('*') if d.is_dir() and not d.is_symlink()]
for folder in folders:
    for f in list(folder.iterdir()):
        if not f.is_symlink() and f.name != f.name.lower():
            link(folder, f.name.lower(), f.name)

real = {f.name.lower(): f.name for f in (xdk / 'include').iterdir() if not f.is_symlink()}
sources = [p for p in (xdk / 'include').iterdir() if not p.is_symlink()]
for root in roots:
    sources += [p for p in root.rglob('*') if p.suffix.lower() in ('.c', '.cpp', '.h', '.hpp', '.inl') and p.is_file()]
for source in sources:
    try:
        text = source.read_bytes()
    except OSError:
        continue
    for m in INCLUDE.finditer(text):
        name = m.group(1).decode('latin1').replace('\\', '/')
        if '/' in name:
            continue
        if name.lower() in real and name != real[name.lower()]:
            link(xdk / 'include', name, real[name.lower()])
print(f'{made} links')
