#!/usr/bin/env python3
"""Lay out the original Xbox build for the console's hard disk.

    python3 tools/xbox_package.py --maps assets/maps --out ~/Downloads/Warthog-xbox

writes OUT/default.xbe (build/xbox/default.xbe, from `ninja xbox`) and
OUT/maps with the maps folder's *.map and loading.tga, hard-linked where the
two folders share a disk (copied where they don't), and OUT/config.toml
with console.bypass_security = true (XNet's insecure mode, which the
internet and the PCs need: the README's "Cross-play"; --secure writes
false). With --game-list-server HOST[:PORT], console.game_list_server names
the game list's server for when warthog.milenko.org does not resolve. An
OUT/config.toml already there keeps its other lines; the game adds every
other setting, at its default, when it starts. The old switch files there
(bypass_security.txt, game_list.txt) are taken into it and removed. Copy
OUT to the console as one folder, e.g. E:\\Games\\Warthog
(port/xbox/README.md).

With --devkit-64mb the copied default.xbe asks the kernel for 64 MB
(XINIT_LIMIT_DEVKIT_MEMORY in the XBE's init flags), so a 128 MB devkit runs
the game with the memory of a stock console. A stock console ignores the flag.

With --map-pack ID=DIR (again for each pack), a community pack of Xbox maps
(Halo CE+: cex) goes in OUT/maps_ID: DIR's *.map and its maplist.txt, the
pack's own maps folder as its release has it. The game plays its maps as
<file>@ID beside the retail ones (README.md, "Xbox map packs").

The maps are the user's own (the NTSC disc's, 01.10.12.2276); none are in
this repository, and no pack's files either.
"""

import argparse
import os
import re
import shutil
import sys
from pathlib import Path


def place(source: Path, destination: Path) -> None:
    if destination.exists():
        destination.unlink()
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


# The XBE header's init flags (cxbe's Xbe.h: dwInitFlags, at 0x124) and the
# bit that limits a devkit's memory to 64 MB (XINIT_LIMIT_DEVKIT_MEMORY)
XBE_INIT_FLAGS_OFFSET = 0x124
XINIT_LIMIT_DEVKIT_MEMORY = 0x4


def set_devkit_memory_limit(image: bytes, limit: bool) -> bytes:
    """image (an XBE) with the init flag that limits a devkit to 64 MB set or
    cleared; every other byte as it was."""
    if len(image) < XBE_INIT_FLAGS_OFFSET + 4 or image[:4] != b"XBEH":
        raise ValueError("not an Xbox executable (XBEH)")
    flags = int.from_bytes(image[XBE_INIT_FLAGS_OFFSET:XBE_INIT_FLAGS_OFFSET + 4], "little")
    flags = flags | XINIT_LIMIT_DEVKIT_MEMORY if limit else flags & ~XINIT_LIMIT_DEVKIT_MEMORY
    patched = bytearray(image)
    patched[XBE_INIT_FLAGS_OFFSET:XBE_INIT_FLAGS_OFFSET + 4] = flags.to_bytes(4, "little")
    return bytes(patched)


# a host or IPv4 address, :port optional
SERVER_PATTERN = r"[A-Za-z0-9.-]+(:[0-9]{1,5})?"
# (the game's port/linux/src/port_config.c's CONFIG_VERSION_LINES: a file without it
# is taken for an older layout)
CONFIG_HEADER = """# Halo settings: tools/xbox_package.py wrote these; the game adds every
# other setting, at its default, when it starts.

# The layout of this file, for the game: leave it.
config_version = 2
"""


def set_console_setting(text: str, key: str, value: str) -> str:
    """text with [console]'s key set to value (a TOML value as written): its
    line, or the line commented out at its default, replaced; else added at
    the section's end (the section added at the file's end if missing)."""
    lines = text.splitlines(keepends=True)
    section = None
    end = None
    pattern = re.compile(r"\s*#?\s*" + re.escape(key) + r"\s*=")
    for index, line in enumerate(lines):
        header = re.match(r"\s*\[([^\]]+)\]", line)
        if header:
            if section == "console":
                break
            section = header.group(1).strip()
            continue
        if section == "console":
            if pattern.match(line):
                lines[index] = f"{key} = {value}\n"
                return "".join(lines)
            if line.strip():
                end = index + 1
    if section != "console" and end is None and "[console]" not in text:
        tail = "" if not text or text.endswith("\n") else "\n"
        return text + tail + f"\n[console]\n{key} = {value}\n"
    lines.insert(end if end is not None else len(lines), f"{key} = {value}\n")
    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--xbe", type=Path, default=Path("build/xbox/default.xbe"))
    parser.add_argument("--maps", type=Path, default=Path("assets/maps"), help="the disc's maps folder")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--secure", action="store_true",
                        help="console.bypass_security = false: XNet's secure system link")
    parser.add_argument("--game-list-server", metavar="HOST[:PORT]",
                        help="console.game_list_server: the list's server if its name does not resolve")
    parser.add_argument("--devkit-64mb", action="store_true",
                        help="set the XBE's flag that limits a devkit's memory to 64 MB, to test as a stock console")
    parser.add_argument("--map-pack", action="append", default=[], metavar="ID=DIR",
                        help="a pack of Xbox maps (its maps folder) for maps_ID: cex=/path/to/Halo CE+ X/maps")
    args = parser.parse_args()
    packs = []
    for value in args.map_pack:
        pack_id, _, folder = value.partition("=")
        if not re.fullmatch(r"[a-z0-9]{1,8}", pack_id) or pack_id in ("ce", "md", "pc", "de", "fr", "es", "it") or \
                not folder or not Path(folder).expanduser().is_dir():
            print(f"--map-pack {value}: ID=DIR, ID 1 to 8 lowercase letters and digits (cex), DIR a folder",
                  file=sys.stderr)
            return 1
        packs.append((pack_id, Path(folder).expanduser()))
    if args.game_list_server and not re.fullmatch(SERVER_PATTERN, args.game_list_server):
        print("--game-list-server: a host or IPv4 address, :port optional", file=sys.stderr)
        return 1

    maps = args.maps.expanduser()
    out = args.out.expanduser()
    if not args.xbe.is_file():
        print(f"no {args.xbe}: run ninja xbox first", file=sys.stderr)
        return 1
    files = sorted(maps.glob("*.map")) + [p for p in [maps / "loading.tga"] if p.is_file()]
    if not any(p.name == "ui.map" for p in files):
        print(f"no ui.map in {maps}", file=sys.stderr)
        return 1
    (out / "maps").mkdir(parents=True, exist_ok=True)
    image = args.xbe.read_bytes()
    try:
        image = set_devkit_memory_limit(image, args.devkit_64mb)
    except ValueError as error:
        print(f"{args.xbe}: {error}", file=sys.stderr)
        return 1
    (out / "default.xbe").write_bytes(image)
    for path in files:
        place(path, out / "maps" / path.name)
    pack_files = 0
    for pack_id, folder in packs:
        (out / f"maps_{pack_id}").mkdir(parents=True, exist_ok=True)
        for path in sorted(folder.glob("*.map")) + [p for p in [folder / "maplist.txt"] if p.is_file()]:
            place(path, out / f"maps_{pack_id}" / path.name)
            pack_files += 1
    config = out / "config.toml"
    text = config.read_text(encoding="utf-8") if config.is_file() else CONFIG_HEADER
    text = set_console_setting(text, "bypass_security", "false" if args.secure else "true")
    game_list_server = args.game_list_server
    old_game_list = out / "game_list.txt"
    if not game_list_server and old_game_list.is_file():
        line = (old_game_list.read_text(encoding="ascii", errors="replace").strip().splitlines() or [""])[0].strip()
        if re.fullmatch(SERVER_PATTERN, line):
            game_list_server = line
    if game_list_server:
        text = set_console_setting(text, "game_list_server", f'"{game_list_server}"')
    config.write_text(text, encoding="utf-8")
    for old in ("bypass_security.txt", "game_list.txt"):
        if (out / old).exists():
            (out / old).unlink()
    print(f"{out}: default.xbe{' (devkit limited to 64 MB)' if args.devkit_64mb else ''}, "
          f"{len(files)} map files{f', {pack_files} map pack files' if packs else ''}, config.toml")
    return 0


if __name__ == "__main__":
    sys.exit(main())
