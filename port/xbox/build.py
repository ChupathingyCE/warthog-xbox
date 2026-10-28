"""Ninja rules for an original Xbox build (``ninja xbox``): the port's
build.py, which the game's configure.py loads (--console-port port/xbox;
the game's tools/console_build.py).

The game compiled for the console it was written for, with clang and
lld-link as OXDK (https://github.com/MrMilenko/OXDK) uses them, into
build/xbox/default.xbe. The game compiles against the January 2002 SDK's
declarations (port/include/xdk), as the other ports do, and links with a
later SDK's libraries, except in two places where those changed:

  - Direct3D: later SDKs renumbered the render and texture stage states
    the game sets, so the game needs a Direct3D of its own time. The
    reconstructed libs/d3d8 is written against the January SDK's headers,
    which are not available, so the build links a prebuilt Aug 2001
    Direct3D library, whose numbering is the January SDK's
    (--xbox-d3d8, below).
  - DirectSound: port/xbox/src/xbox_dsound.c translates the January SDK's
    mix bin masks.

From this repository's root, with the game in chupathingyce/ (a submodule):

    python3 chupathingyce/configure.py --console-port port/xbox --xbox-d3d8 LIBRARY
    ninja xbox

The build is generated when OXDK and the Direct3D library are found
(the port's options):

    --oxdk DIR          OXDK's checkout (default OXDK_DIR, or ~/OXDK): its
                        XDK libraries (xbox/xdk/lib) and headers, and cxbe
    --xbox-d3d8 LIBRARY an Aug 2001 Direct3D library (d3d8ntpr.lib, the
                        kernel's boot animation build of that time)

clang and lld-link come from LLVM_DIR, or Homebrew's llvm.

None of the SDK's files belong in this repository. See port/xbox/README.md.
"""

import os
import shlex
from pathlib import Path
from typing import Any, List, Optional

from tools.console_build import ConsolePort, console_defines, console_https_flags, console_https_sources, relative
from tools.ninja_syntax import Writer
from tools.version import version
from tools.windows_build import COMMENT, EXPORTED_INLINE

# this repository's tools (the build's stamp and generated sources), beside
# the port
TOOLS_DIR = relative(Path(__file__).resolve().parents[2] / "tools")

GAME_FLAGS = [
    "-target", "i386-pc-windows-msvc",
    "-march=pentium3",
    "-fms-extensions",
    "-fms-compatibility",
    "-fms-compatibility-version=13.10",
    "-O2",
    # the game keeps EBP frames (MSVC /Oy-): its stack walker follows them,
    # for the crash reports in debug.txt. A function that calls no other
    # (the vector math, the small getters) is built without one: three
    # instructions fewer on each call and a seventh register for its code.
    # A crash inside one still names it (the processor's address) and the
    # chain from its caller's caller on; only that caller is not named
    "-fno-omit-frame-pointer",
    "-momit-leaf-frame-pointer",
    "-fcommon",
    # each function in a section of its own, so that the linker leaves out
    # the ones nothing calls (/opt:ref, LINK_FLAGS): the debug decomp has
    # about 1,400 of them (editor and tool code, unused math), 240 KB of the
    # image a 64 MB console has back. The data stays as it was, in one
    # section a unit: the decomp's globals keep their neighbors
    "-ffunction-sections",
    "-D_XBOX", "-D_X86_", "-D_NTOS_", "-D_MT",
    # the console's own capacity (port/linux/include/halo_port_capacity.h)
    "-DHALO_XBOX_CONSOLE",
    # Delta Peer as a client (port/linux/src/delta_peer_game.c)
    "-DHALO_CONSOLE_DELTA",
    # the memory class, 64 MB or more (port/linux/include/halo_console_memory.h,
    # port/xbox/src/xbox_memory.c)
    "-DHALO_CONSOLE_MEMORY_CLASS",
    "-w",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=incompatible-function-pointer-types",
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=implicit-int",
    "-Wno-error=return-type",
]

# the platform units, compiled with the SDK's own headers (OXDK's flags)
SUPPORT_FLAGS = [
    "-target", "i386-pc-windows-msvc",
    "-march=pentium3",
    "-fms-extensions",
    "-fms-compatibility",
    "-fms-compatibility-version=13.10",
    "-O2",
    "-D_XBOX", "-D_X86_", "-DWIN32_LEAN_AND_MEAN", "-D_NTOS_", "-D_MT",
    "-Wno-microsoft-include", "-Wno-pragma-pack", "-Wno-ignored-pragmas",
    "-Wno-deprecated-declarations", "-Wno-writable-strings", "-Wno-microsoft-cast",
    "-Wno-unknown-pragmas", "-Wno-extra-tokens", "-Wno-nonportable-include-path",
    "-Wno-typedef-redefinition", "-Wno-missing-prototype-for-cc", "-Wno-comment",
    # clang's va_start in place of the SDK's address arithmetic, which breaks
    # once a variadic function is inlined (port/xbox/include/xbox_stdarg.h)
    "-include", "$xbox_port/include/xbox_stdarg.h",
    # a format and its arguments that disagree are errors (the console's C
    # runtime has no %z, %ll, %j or %hh either)
    "-Wformat", "-Werror=format",
    # (the functions nothing calls left out, as the game's: GAME_FLAGS)
    "-ffunction-sections",
]
# the platform code that runs at the start, at a join or once a second at
# most, built for size: the settings' TOML parser and table, the tunnel's key
# exchange and signatures (Monocypher, p2p_crypto.c), the game list, the
# claim and the player key. Monocypher alone goes from 83 KB to 18 KB
COLD_FLAGS = ["-Os"]
COLD_SOURCES = (
    "xbox_claim.c", "xbox_claim_run.c", "xbox_config.c", "xbox_coop_name.c", "xbox_direct_join.c",
    "xbox_game_list_fetch.c", "xbox_game_list_parse.c", "xbox_game_list_text.c", "xbox_log_tag.c",
    "xbox_https.c", "xbox_memory.c", "xbox_player_key.c", "xbox_stack.c", "xbox_xlink.c",
    "monocypher.c", "monocypher-ed25519.c", "p2p_crypto.c", "port_config.c", "tomlc17.c",
)
SUPPORT_CXX_FLAGS = ["-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]

LINK_FLAGS = [
    "/nologo", "/subsystem:windows", "/fixed:no", "/base:0x00010000", "/stack:1048576",
    "/machine:x86", "/entry:mainCRTStartup",
    # the functions nothing calls are left out (-ffunction-sections), and no
    # two that compile the same are folded into one: the game compares
    # function pointers (hs.c tells a script macro by its parser's)
    "/opt:ref,noicf", "/nodefaultlib", "/safeseh:no",
    "/merge:.edata=.edataxb", "/errorlimit:0", "--rsp-quoting=posix",
]
# (the SDK's D3DX is built on its own Direct3D; port/xbox/src/xbox_d3dx.c has
# the four functions the game calls)
LIBRARIES = [
    "libcmt.lib", "oldnames.lib", "xboxkrnl.lib", "dsound.lib", "xnet.lib", "xapilib.lib", "xkbd.lib",
]
# the game's directories the build takes from (under the game's root)
XDK_INCLUDE = Path("port/include/xdk")
LINUX_SRC = Path("port/linux/src")
PORT_LINUX_INCLUDE = Path("port/linux/include")
PORT_LINUX_GAME = Path("port/linux/game")
# internet play's sources (port/xbox/p2p/platform.h)
KCP_DIR = Path("port/third_party/kcp")
MONOCYPHER_DIR = Path("port/third_party/monocypher")
P2P_SOURCES = [
    LINUX_SRC / "p2p.c", LINUX_SRC / "p2p_probe.c", LINUX_SRC / "p2p_signal.c", LINUX_SRC / "p2p_crypto.c", KCP_DIR / "ikcp.c",
    MONOCYPHER_DIR / "monocypher.c", MONOCYPHER_DIR / "monocypher-ed25519.c",
]
# Delta Peer's (docs/delta.md), as the desktop builds compile them: the wire
# format, a session as a client, and the game's session over the game's
# sockets, which wires play together (delta_compat.c), and the signed legacy
# table (delta.c, without its thread: the game list's thread fetches it)
DELTA_SOURCES = [LINUX_SRC / "delta_wire.c", LINUX_SRC / "delta_peer.c", LINUX_SRC / "delta_chat.c", LINUX_SRC / "delta_chat_words.c",
                 LINUX_SRC / "delta_chat_bridge.c",
                 LINUX_SRC / "delta_peer_game.c", LINUX_SRC / "delta_compat.c",
                 # the signed legacy table, fetched over HTTPS
                 # (HALO_CONSOLE_HTTPS: port/xbox/src/xbox_https.c asks)
                 LINUX_SRC / "delta.c"]
# the settings, D:\config.toml: the desktop builds' port_config.c and its
# TOML parser (tools/linux_build.py's TOML_DIR)
CONFIG_SOURCE = LINUX_SRC / "port_config.c"
TOMLC17_DIR = Path("port/third_party/tomlc17")
# the port's zlib (tools/linux_build.py's ZLIB_*): what the game's map
# decompression inflates with (cache_files_decompress_windows.c), its names
# prefixed z_
ZLIB_DIR = Path("port/third_party/zlib")
ZLIB_SOURCES = ("adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "uncompr.c", "zutil.c")
ZLIB_DEFINES = ("-DZ_PREFIX", "-Dz_errmsg=z_port_errmsg")
# the game's C++ sources (game_sources has its C only)
XBOX_GAME_CXX_SOURCES = [Path("source/main/d3d_intimacy.cpp")]
# port/linux/game's sources the console builds too
XBOX_PORT_GAME_SOURCES = [
    "audio_probe.c", "chat.c", "follow_camera.c", "map_families.c", "network_damage.c", "network_distributed.c", "network_game_layout.c",
    "network_objects.c", "network_rules.c", "pal_tags.c", "powerup_timers.c", "render_interpolation.c",
    # the menus' map list and the Xbox map packs (maps_cex: Halo CE+), as
    # the desktop builds have them (HALO_MAP_LIST, below)
    "map_packs.c", "ui_map_list.c",
]
# the menus' map list filled by the port, with the map packs' maps
# (port/linux/game/ui_map_list.c, map_packs.c), on the console too
MAP_LIST_DEFINES = ["-DHALO_MAP_LIST"]
# d3d_intimacy.cpp reads the device as the January library named it; the
# kernel's build of the Aug 2001 library has it in its own namespace (the
# same layout)
ALTERNATE_NAMES = [
    "/alternatename:?g_Device@D3D@@3VCDevice@1@A=?g_Device@D3DK@@3VCDevice@1@A",
]


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return shlex.quote(text)


def add_arguments(parser: Any) -> None:
    """the port's own configure.py options"""
    parser.add_argument(
        "--xbox-d3d8",
        metavar="LIBRARY",
        type=Path,
        help="an Aug 2001 Direct3D library (d3d8ntpr.lib) for the original Xbox build, `ninja xbox` (port/xbox/README.md)",
    )
    parser.add_argument(
        "--oxdk",
        metavar="DIR",
        type=Path,
        help="OXDK checkout for `ninja xbox` (default: OXDK_DIR, or ~/OXDK)",
    )


def _oxdk_dir(sln: Any) -> Path:
    value = getattr(sln.args, "oxdk", None) or os.environ.get("OXDK_DIR") or Path.home() / "OXDK"
    return Path(value).expanduser().resolve()


def _d3d8_library(sln: Any) -> Optional[Path]:
    value = getattr(sln.args, "xbox_d3d8", None)
    return Path(value).expanduser().resolve() if value else None


def _llvm_tool(name: str) -> str:
    llvm = os.environ.get("LLVM_DIR")
    for directory in ([Path(llvm) / "bin"] if llvm else []) + [Path("/opt/homebrew/opt/llvm/bin"),
                                                             Path("/usr/local/opt/llvm/bin")]:
        if (directory / name).is_file():
            return str(directory / name)
    return name


def _inline_export_wrapper(port: ConsolePort, source: Path, build_dir: Path) -> Path:
    """As the Windows build's (tools/windows_build.py): a unit that defines
    an __inline function other units call through a prototype is compiled
    through a wrapper that takes the functions' addresses, so that clang
    emits them (as COMDATs) even where it inlined every call."""
    if source.suffix != ".c":
        return source
    names = EXPORTED_INLINE.findall(COMMENT.sub("", source.read_text(encoding="utf-8", errors="replace")))
    if not names:
        return source
    wrapper = port.output_path(build_dir / "inline_exports", source)
    text = (
        "/* generated by port/xbox/build.py: see _inline_export_wrapper */\n"
        f'#include "{source.resolve().as_posix()}"\n'
        "static void *const halo_xbox_inline_exports[] __attribute__((used)) = {\n"
        + "".join(f"\t(void *){name},\n" for name in names)
        + "};\n"
    )
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    if not wrapper.is_file() or wrapper.read_text(encoding="utf-8") != text:
        wrapper.write_text(text, encoding="utf-8")
    return wrapper


def configure_inputs(port: ConsolePort) -> List[Path]:
    """Files whose change must re-run configure.py."""
    return [port.path("src"), port.path("game"), port.path("p2p")]


def generate(n: Writer, sln: Any, port: ConsolePort) -> None:
    game = port.game
    PORT_DIR = port.root
    oxdk = _oxdk_dir(sln)
    xdk = oxdk / "xbox" / "xdk"
    cxbe = oxdk / "xbox" / "tools" / "cxbe" / "cxbe"
    d3d8 = _d3d8_library(sln)
    if not (xdk / "lib" / "xboxkrnl.lib").is_file() or not d3d8 or not d3d8.is_file():
        return
    build_dir: Path = sln.build_dir / "xbox"
    obj_dir = build_dir / "obj"
    gen_dir = build_dir / "gen"
    exe = build_dir / "halo.exe"
    xbe = build_dir / "default.xbe"

    n.comment("Original Xbox build (ninja xbox)")
    n.variable("xbox_port", _quote(PORT_DIR))
    n.variable("xbox_cc", _llvm_tool("clang"))
    n.variable("xbox_link", _llvm_tool("lld-link"))
    n.rule(
        name="xbox_cc",
        command="$xbox_cc -MD -MF $out.d $cflags -c $in -o $out",
        description="XBOX CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="xbox_lld",
        command="$xbox_link $ldflags /map:$map /out:$out @$out.rsp",
        description="XBOX LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline $libs",
    )
    n.rule(
        name="xbox_cxbe",
        command=f"{_quote(cxbe)} -MODE:RETAIL -TITLE:\"Halo\" -LIMIT64MB:no -OUT:$out $in",
        description="XBOX XBE $out",
    )
    n.rule(
        name="xbox_source",
        command=f"$python {_quote(TOOLS_DIR / 'xbox_sources.py')} $kind $in $out",
        description="XBOX GEN $out",
        restat=True,
    )
    n.rule(
        name="xbox_msvc_semantics",
        command=f"$python {_quote(game / 'tools' / 'linux_msvc_semantics.py')} --output $out $scan",
        description="XBOX MSVC SEMANTICS $out",
        restat=True,
    )

    # the stamp of this build (xbox_memory.c logs it): written on every
    # ninja run, from the commit and the time (tools/xbox_stamp.py)
    stamp_source = gen_dir / "xbox_build_stamp.c"
    n.rule(
        name="xbox_stamp",
        command=f"$python {_quote(TOOLS_DIR / 'xbox_stamp.py')} $out",
        description="XBOX STAMP $out",
        restat=True,
    )
    n.build(outputs="xbox_stamp_always", rule="phony")
    n.build(outputs=stamp_source, rule="xbox_stamp", implicit=[TOOLS_DIR / "xbox_stamp.py", "xbox_stamp_always"])

    semantics_header = build_dir / "halo_msvc_semantics.h"
    game_headers = port.game_headers()
    # (the file-scope struct tags only: clang's MSVC target already gives a C
    # __inline function MSVC's pick-any COMDAT, which the other ports' weak
    # symbols stand in for)
    n.build(outputs=semantics_header, rule="xbox_msvc_semantics",
            implicit=[game / "tools" / "linux_msvc_semantics.py", *game_headers],
            variables={"scan": f"--tags {_quote(game / 'source')}"})

    objects: List[Path] = []
    # the game's file names as the game spells them ("source/game/game.c",
    # not "chupathingyce/source/game/game.c"): the log's and the asserts'
    # names stay the desktop builds', and the build is the same wherever the
    # game is checked out
    file_names = f"-fno-file-reproducible -fmacro-prefix-map={_quote(game.as_posix() + '/')}=" if game != Path(".") else ""

    def add_object(source: Path, cflags: str, implicit: List[Path] = (), compiled: Optional[Path] = None) -> None:
        obj = obj_dir / source.relative_to(build_dir) if build_dir in source.parents else port.output_path(obj_dir, source)
        obj = obj.with_suffix(obj.suffix + ".obj")
        objects.append(obj)
        n.build(outputs=obj, rule="xbox_cc", inputs=compiled or source,
                implicit=[*implicit, *([source] if compiled and compiled != source else [])],
                variables={"cflags": f"{cflags} {file_names}".rstrip()})

    # configure.py's console options (--release, --console-players,
    # --console-debug-tools, --console-no-debug-asserts: tools/console_build.py)
    defines = console_defines(sln)
    players_flag = next(define for define in defines if define.startswith("-DHALO_CONSOLE_PLAYERS="))
    prefix_header = PORT_DIR / "include" / "halo_xbox_prefix.h"
    # the game's sources, defines and include directories (port/linux/port.json)
    config = port.load_port_config()
    game_cflags = " ".join([
        " ".join(GAME_FLAGS),
        *defines,
        *MAP_LIST_DEFINES,
        f"-include {_quote(prefix_header)}", f"-include {_quote(semantics_header)}", f"-iquote {_quote(game)}",
        port.game_defines_and_includes(config, _quote), f"-idirafter {_quote(game / XDK_INCLUDE)}",
        f"-idirafter {_quote(xdk / 'include')}",
        # the port's own headers (halo_menus.h, halo_keyboard.h), after the
        # SDK's so its C runtime headers win
        f"-idirafter {_quote(game / PORT_LINUX_INCLUDE)}",
        # the PC port's game headers the shared sources include (network
        # co-op's: port/xbox/game/xbox_coop.c answers them on the console)
        f"-idirafter {_quote(game / PORT_LINUX_GAME)}",
    ])
    for source, flags in port.game_units(sln, config):
        # (the script console is not the console's: telnet_console.h)
        if port.game_relative(source) == "source/networking/telnet_console.c":
            continue
        add_object(source, " ".join(["-std=gnu89", game_cflags, *flags]), [semantics_header, prefix_header],
                   _inline_export_wrapper(port, source, build_dir))
    # the game's C++, which the native builds replace (d3d8_gl.c): the frame
    # counter read out of the console's Direct3D device
    for source in XBOX_GAME_CXX_SOURCES:
        add_object(game / source, f"-fno-exceptions {game_cflags}", [semantics_header, prefix_header])
    # one pick-any copy of each header inline that units call through a
    # prototype (see the file)
    add_object(game / PORT_LINUX_GAME / "msvc_comdat.c", f"-std=gnu89 {game_cflags}",
               [semantics_header, prefix_header])
    # the native builds' game code the console plays the same (their netcode,
    # so it plays with them; PAL tags; frame interpolation), but not their
    # screens of the game list, which draw through their platform layer
    for name in XBOX_PORT_GAME_SOURCES:
        add_object(game / PORT_LINUX_GAME / name, f"-std=gnu89 {game_cflags}", [semantics_header, prefix_header])

    # the console's own game code: its Online Games screen
    for source in sorted((PORT_DIR / "game").glob("*.c")):
        add_object(source, f"-std=gnu89 {game_cflags}", [semantics_header, prefix_header])

    support_cflags = " ".join([*SUPPORT_FLAGS, players_flag, f"-iquote {_quote(game)}", f"-I{_quote(xdk / 'include')}"])
    for source in sorted((PORT_DIR / "src").glob("*.c")):
        add_object(source, " ".join([support_cflags, *(COLD_FLAGS if source.name in COLD_SOURCES else [])]))
    add_object(stamp_source, support_cflags)
    for source in sorted((PORT_DIR / "src").glob("*.cpp")):
        add_object(source, " ".join([support_cflags, *SUPPORT_CXX_FLAGS]))
    for kind, source in (("common", game / LINUX_SRC / "halo_linker_common.c"), ("bink", game / LINUX_SRC / "bink_null.c")):
        generated = gen_dir / f"xbox_{source.name}"
        n.build(outputs=generated, rule="xbox_source", inputs=source, implicit=[TOOLS_DIR / "xbox_sources.py"],
                variables={"kind": kind})
        add_object(generated, support_cflags)

    zlib_cflags = " ".join([*SUPPORT_FLAGS, *ZLIB_DEFINES, "-w", f"-I{_quote(xdk / 'include')}"])
    for name in ZLIB_SOURCES:
        add_object(game / ZLIB_DIR / name, zlib_cflags)

    # internet play (the desktop builds' tunnel, joining only: port/xbox/src/
    # xbox_p2p.c is its platform layer), its sources as they are, with
    # port/xbox/p2p's headers in place of the Linux layer's
    p2p_cflags = " ".join([*SUPPORT_FLAGS, players_flag, "-std=gnu11", f"-iquote {_quote(game)}", f"-I{_quote(PORT_DIR / 'p2p')}",
                           f"-I{_quote(game / LINUX_SRC)}", f"-iquote {_quote(game / PORT_LINUX_INCLUDE)}",
                           f"-I{_quote(game / KCP_DIR)}", f"-I{_quote(game / MONOCYPHER_DIR)}", f"-I{_quote(xdk / 'include')}"])
    for source in P2P_SOURCES:
        generated = gen_dir / "p2p" / source.name
        n.build(outputs=generated, rule="xbox_source", inputs=game / source, implicit=[TOOLS_DIR / "xbox_sources.py"],
                variables={"kind": "copy"})
        add_object(generated, " ".join([p2p_cflags, *(COLD_FLAGS if source.name in COLD_SOURCES else [])]),
                   sorted((PORT_DIR / "p2p").glob("*.h")))

    # Delta Peer: as internet play's sources, with the console's limits
    # (HALO_XBOX_CONSOLE), the C99 calls its C runtime lacks, and the port's
    # headers (delta.h, halo_port_limits.h) after the SDK's
    # (-Os: the console's 64 MB keeps every byte of its code)
    delta_cflags = " ".join([p2p_cflags, "-Os", "-DHALO_XBOX_CONSOLE", "-DHALO_CONSOLE_DELTA", "-DHALO_CONSOLE_HTTPS",
                             f'-DHALO_VERSION=\\"{version()}\\"', *console_https_flags(game),
                             f"-include {_quote(PORT_DIR / 'p2p' / 'xbox_stdio.h')}",
                             f"-idirafter {_quote(game / PORT_LINUX_INCLUDE)}"])
    for source in DELTA_SOURCES:
        generated = gen_dir / "p2p" / source.name
        n.build(outputs=generated, rule="xbox_source", inputs=game / source, implicit=[TOOLS_DIR / "xbox_sources.py"],
                variables={"kind": "copy"})
        add_object(generated, delta_cflags, sorted((PORT_DIR / "p2p").glob("*.h")))

    # the settings (port/xbox/src/xbox_config.c): port_config.c as it is,
    # copied as internet play's are, with the console's headers first
    config_cflags = " ".join([p2p_cflags, *COLD_FLAGS, "-DHALO_XBOX_CONSOLE", f"-I{_quote(game / TOMLC17_DIR)}"])
    generated = gen_dir / "p2p" / CONFIG_SOURCE.name
    n.build(outputs=generated, rule="xbox_source", inputs=game / CONFIG_SOURCE, implicit=[TOOLS_DIR / "xbox_sources.py"],
            variables={"kind": "copy"})
    add_object(generated, config_cflags, sorted((PORT_DIR / "p2p").glob("*.h")))
    add_object(game / TOMLC17_DIR / "tomlc17.c", " ".join([*SUPPORT_FLAGS, *COLD_FLAGS, "-std=gnu11", "-w", "-include",
                                                           _quote(PORT_DIR / "include" / "xbox_c99.h"),
                                                           f"-I{_quote(xdk / 'include')}"]),
               [PORT_DIR / "include" / "xbox_c99.h"])

    # HTTPS (port/xbox/src/xbox_https.c): the game's shared client and Mbed
    # TLS with the consoles' configuration (the game's docs/console-https.md),
    # built for size
    https_cflags = " ".join([*SUPPORT_FLAGS, *COLD_FLAGS, "-std=gnu11", "-w", "-include",
                             _quote(PORT_DIR / "include" / "xbox_c99.h"), *console_https_flags(game, windows_crt=True),
                             f"-I{_quote(xdk / 'include')}"])
    for source in console_https_sources():
        add_object(game / source, https_cflags, [PORT_DIR / "include" / "xbox_c99.h"])

    n.build(
        outputs=exe,
        rule="xbox_lld",
        inputs=objects,
        implicit=[d3d8],
        implicit_outputs=[build_dir / "halo.map"],
        variables={
            "ldflags": " ".join([*LINK_FLAGS, *ALTERNATE_NAMES, f"/libpath:{_quote(xdk / 'lib')}"]),
            "map": _quote(build_dir / "halo.map"),
            "libs": " ".join([_quote(d3d8), *LIBRARIES]),
        },
    )
    n.build(outputs=xbe, rule="xbox_cxbe", inputs=exe, implicit=[cxbe])
    n.build(outputs="xbox", rule="phony", inputs=xbe)
    n.newline()
