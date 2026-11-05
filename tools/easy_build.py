#!/usr/bin/env python3
"""Build Warthog for the original Xbox, step by step.

    python3 tools/easy_build.py            asks a few questions, then builds
    python3 tools/easy_build.py --check    only checks what the build needs
    python3 tools/easy_build.py --yes ...  no questions (for scripts)

It checks the tools, asks for the files you must supply yourself, runs the
game's configure.py (chupathingyce/, a submodule) and `ninja xbox`, and can
lay out the finished game for the console's hard disk (tools/xbox_package.py).
Your answers are remembered in .easy_build.json, which git ignores. Python 3 and its standard library only.

Nothing from Microsoft's SDKs is in this repository and none may be
committed: you supply your own copy of the Xbox SDK (in OXDK), the Aug 2001
Direct3D library and your own disc's maps. docs/building.md has the same
steps by hand.
"""

import argparse
import collections
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent
STATE_FILE = ROOT / ".easy_build.json"
LOG_FILE = ROOT / "build" / "easy_build.log"
OXDK_URL = "https://github.com/MrMilenko/OXDK"
UI_MAP_SIZE = 14145536
MAP_BUILDS = (b"01.10.12.2276", b"01.01.14.2342")
PLAYERS = (16, 32, 64)
# the console build's players (the game's configure.py --console-players):
# 32 by default, 16 as the Xbox's own game had, 64 a test build
DEFAULT_PLAYERS = 32
# the game's configure.py (the submodule) and the port's directory
GAME_CONFIGURE = "chupathingyce/configure.py"
PORT = "port/xbox"

HOMEBREW_LLVM = ("/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin")


# ---------------------------------------------------------------------------
# pure helpers (port/xbox/tests/easy_build_test.py)

def parse_players(text: str) -> Optional[int]:
    """16, 32 or 64 from what the user typed, else None."""
    try:
        value = int(str(text).strip())
    except ValueError:
        return None
    return value if value in PLAYERS else None


def parse_yes_no(text: str, default: bool) -> bool:
    """y/yes/n/no (any case); an empty or unclear answer is the default."""
    answer = str(text).strip().lower()
    if answer in ("y", "yes"):
        return True
    if answer in ("n", "no"):
        return False
    return default


def default_out_name(date: datetime.date, commit: str) -> str:
    name = f"warthog-xbox-{date.strftime('%Y%m%d')}"
    return f"{name}-{commit}" if commit else name


def clean_path(text: str) -> str:
    """A path as pasted: whitespace, quotes and a drag-and-drop's escapes removed."""
    value = text.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        value = value[1:-1]
    if os.name != "nt":
        value = re.sub(r"\\(.)", r"\1", value)
    return value


def check_maps(maps: Path) -> Tuple[bool, List[str]]:
    """(usable, notes) for a maps folder: it needs a ui.map, and the notes warn
    when ui.map is not the NTSC disc's (01.10.12.2276, 14,145,536 bytes)."""
    ui = maps / "ui.map"
    if not ui.is_file():
        return False, [f"no ui.map in {maps}"]
    notes: List[str] = []
    size = ui.stat().st_size
    if size != UI_MAP_SIZE:
        notes.append(f"ui.map is {size:,} bytes, not {UI_MAP_SIZE:,}: these are not the North American (NTSC, "
                     "01.10.12.2276) disc's maps. The game may still run them, but online play needs "
                     "the NTSC maps.")
    try:
        with open(ui, "rb") as handle:
            head = handle.read(2048)
        if not any(build in head for build in MAP_BUILDS):
            notes.append("ui.map's header names neither build 01.10.12.2276 (NTSC) nor 01.01.14.2342 (PAL).")
    except OSError as error:
        notes.append(f"could not read ui.map: {error}")
    if not list(maps.glob("*.map")) or len(list(maps.glob("*.map"))) < 2:
        notes.append("the folder has hardly any maps: copy the whole maps folder from your disc.")
    return True, notes


def configure_args(d3d8: str, oxdk: str, players: int, release: bool, debug_tools: bool,
                   asserts: bool = False) -> List[str]:
    """configure.py's arguments. Without asserts, the build testers get
    (--console-no-debug-asserts): the debug decomp's asserts compiled out but
    for the memory checks, about 370 KB less image."""
    args = [GAME_CONFIGURE, "--console-port", PORT, "--xbox-d3d8", d3d8, "--oxdk", oxdk,
            "--console-players", str(players)]
    if not asserts:
        args.append("--console-no-debug-asserts")
    if release:
        args.append("--release")
    if debug_tools:
        args.append("--console-debug-tools")
    return args


def package_args(maps: str, out: str, devkit_64mb: bool) -> List[str]:
    args = ["tools/xbox_package.py", "--maps", maps, "--out", out]
    if devkit_64mb:
        args.append("--devkit-64mb")
    return args


def find_tool(name: str, extra_dirs: Sequence[str] = (), path: Optional[str] = None) -> Optional[str]:
    """A tool's full path: the extra directories first (LLVM_DIR, Homebrew's llvm), then PATH."""
    for directory in extra_dirs:
        for candidate in (name, name + ".exe"):
            full = Path(directory) / candidate
            if full.is_file() and os.access(full, os.X_OK):
                return str(full)
    return shutil.which(name, path=path)


def hint_for_failure(output: str) -> str:
    """A hint for the likeliest cause of a failed build, from its last lines."""
    text = output.lower()
    if "cannot open file" in text or ("could not open" in text and ".lib" in text):
        return ("A library could not be opened. Check that OXDK's xbox/xdk/lib holds your Xbox SDK's libraries "
                "and that the d3d8ntpr.lib path is right.")
    if "no such file or directory" in text and ("include" in text or ".h" in text):
        return "A header is missing. Check that OXDK's xbox/xdk/include holds your Xbox SDK's headers."
    if "lld-link" in text and ("not found" in text or "no such file" in text):
        return "lld-link was not found. Install LLVM's lld (see --check) or set LLVM_DIR."
    if "clang" in text and ("not found" in text or "no such file" in text):
        return "clang was not found. Install LLVM (see --check) or set LLVM_DIR."
    if "cxbe" in text:
        return "cxbe failed. Build it: make -C <OXDK>/xbox/tools/cxbe"
    return ("The full output is in build/easy_build.log. If you report this, attach that file "
            "and say which OS and LLVM version you have (#warthog-support on the Discord).")


def progress_fraction(line: str) -> Optional[Tuple[int, int]]:
    match = re.match(r"^\[(\d+)/(\d+)\]", line)
    return (int(match.group(1)), int(match.group(2))) if match else None


def load_state(path: Path = STATE_FILE) -> Dict[str, object]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_state(state: Dict[str, object], path: Path = STATE_FILE) -> None:
    try:
        path.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except OSError as error:
        say(f"(could not save your answers to {path.name}: {error})")


# ---------------------------------------------------------------------------
# output and questions

class Console:
    interactive = True
    use_color = False


def say(text: str = "") -> None:
    print(text, flush=True)


def heading(text: str) -> None:
    say()
    line = f"== {text} =="
    say(f"\033[1m{line}\033[0m" if Console.use_color else line)


def ask(question: str, default: str = "") -> str:
    """The answer, or the default with --yes (or no terminal)."""
    if not Console.interactive:
        return default
    suffix = f" [{default}]" if default else ""
    try:
        answer = input(f"{question}{suffix}: ").strip()
    except EOFError:
        return default
    return answer or default


def ask_yes_no(question: str, default: bool) -> bool:
    if not Console.interactive:
        return default
    return parse_yes_no(ask(f"{question} ({'Y/n' if default else 'y/N'})"), default)


def ask_path(question: str, default: str, must_exist: bool = True) -> str:
    while True:
        value = clean_path(ask(question, default))
        if not value:
            return ""
        full = Path(value).expanduser()
        if not must_exist or full.exists():
            return str(full)
        say(f"  {full} does not exist.")
        if not Console.interactive:
            return str(full)


# ---------------------------------------------------------------------------
# prerequisite checks

def os_name() -> str:
    if sys.platform == "darwin":
        return "macos"
    if sys.platform.startswith("win"):
        return "windows"
    return "linux"


class Check:
    def __init__(self, name: str, ok: bool, detail: str, why: str = "", fix: str = "") -> None:
        self.name, self.ok, self.detail, self.why, self.fix = name, ok, detail, why, fix


def tool_version(path: str) -> str:
    try:
        out = subprocess.run([path, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             universal_newlines=True, timeout=20).stdout
        return out.strip().splitlines()[0] if out.strip() else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def llvm_dirs() -> List[str]:
    dirs = []
    if os.environ.get("LLVM_DIR"):
        dirs.append(str(Path(os.environ["LLVM_DIR"]) / "bin"))
    return dirs + list(HOMEBREW_LLVM)


def llvm_fix(tool: str) -> str:
    system = os_name()
    if system == "macos":
        return "brew install llvm lld ninja"
    if system == "windows":
        return "winget install LLVM.LLVM   (then open a new terminal; or set LLVM_DIR to LLVM's folder)"
    return ("sudo apt install clang lld ninja-build   (Fedora: sudo dnf install clang lld ninja-build; "
            "if only lld-link-NN exists, link it as lld-link)")


def check_python() -> Check:
    version = sys.version_info
    ok = version >= (3, 7)
    return Check("Python 3", ok, f"{version[0]}.{version[1]}.{version[2]}",
                 "runs configure.py and the packaging script", "install Python 3.7 or newer from python.org")


def check_ninja() -> Check:
    path = find_tool("ninja")
    system = os_name()
    fix = {"macos": "brew install ninja", "windows": "winget install Ninja-build.Ninja"}.get(
        system, "sudo apt install ninja-build")
    return Check("ninja", bool(path), path or "not found", "runs the build that configure.py writes", fix)


def check_llvm() -> List[Check]:
    results = []
    for tool, why in (("clang", "compiles the game for the Xbox's x86 processor"),
                      ("lld-link", "links the game into an Xbox executable")):
        path = find_tool(tool, llvm_dirs())
        detail = "not found"
        if path:
            detail = path
            version = tool_version(path)
            if version:
                detail += f"  ({version})"
        results.append(Check(tool, bool(path), detail, why, llvm_fix(tool)))
    return results


def check_oxdk(oxdk: str) -> List[Check]:
    base = Path(oxdk).expanduser() if oxdk else None
    clone = f"git clone {OXDK_URL} ~/OXDK"
    if not base or not base.is_dir():
        return [Check("OXDK", False, f"not found{(' at ' + str(base)) if base else ''}",
                      "the Xbox toolchain glue: it holds the cxbe tool that makes the .xbe, and your SDK files",
                      f"{clone}   (public repository: {OXDK_URL}), or pass --oxdk DIR")]
    results = [Check("OXDK", True, str(base))]
    cxbe = base / "xbox" / "tools" / "cxbe" / "cxbe"
    if os_name() == "windows":
        cxbe = cxbe.with_suffix(".exe")
    results.append(Check("OXDK cxbe", cxbe.is_file(), str(cxbe) if cxbe.is_file() else "not built",
                         "turns the linked program into default.xbe",
                         f"make -C {base / 'xbox' / 'tools' / 'cxbe'}   (needs make and a C++ compiler)"))
    krnl = base / "xbox" / "xdk" / "lib" / "xboxkrnl.lib"
    results.append(Check("Xbox SDK libraries", krnl.is_file(),
                         str(krnl.parent) if krnl.is_file() else f"{krnl} is missing",
                         "the original Xbox SDK's libraries; Microsoft's files, which we can't distribute. "
                         "You supply your own copy (the 5933 SDK)",
                         f"copy your own SDK's lib and include folders into {base / 'xbox' / 'xdk'}, "
                         "as OXDK's README describes"))
    return results


def check_d3d8(path: str) -> Check:
    ok = bool(path) and Path(path).expanduser().is_file()
    return Check("d3d8ntpr.lib", ok, path if ok else (f"{path} is missing" if path else "not set"),
                 "the Aug 2001 Direct3D library from the original Xbox SDK. It is Microsoft's file, which we "
                 "can't distribute: you supply your own copy (the aug01 i386 build). The game needs a Direct3D "
                 "of its own time (port/xbox/README.md, \"Direct3D\")",
                 "give its path with --d3d8 PATH, or when asked")


def check_game() -> Check:
    path = ROOT / GAME_CONFIGURE
    return Check("the game", path.is_file(), str(path.parent) if path.is_file() else f"{path} is missing",
                 "the game's source (ChupathingyCE/chupathingyce), a git submodule of this repository",
                 "git submodule update --init   (or clone with --recursive)")


def run_checks(state: Dict[str, object], oxdk: str, d3d8: str) -> List[Check]:
    checks = [check_python(), check_ninja(), check_game(), *check_llvm(), *check_oxdk(oxdk), check_d3d8(d3d8)]
    return checks


def print_checks(checks: Sequence[Check]) -> bool:
    ok = True
    for check in checks:
        say(f"  [{'ok' if check.ok else 'MISSING'}] {check.name}: {check.detail}")
        if not check.ok:
            ok = False
            if check.why:
                say(f"        what it is: {check.why}")
            if check.fix:
                say(f"        how to get it: {check.fix}")
    return ok


# ---------------------------------------------------------------------------
# running things

def run_logged(label: str, command: Sequence[str], env: Optional[Dict[str, str]] = None) -> Tuple[int, List[str]]:
    """Run a command in the repository, showing progress; the full output goes to the log."""
    LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    tail = collections.deque(maxlen=25)  # type: collections.deque
    last_percent = -1
    say(f"{label} ...")
    with open(LOG_FILE, "a", encoding="utf-8") as log:
        log.write(f"\n$ {' '.join(command)}\n")
        proc = subprocess.Popen(list(command), cwd=str(ROOT), env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, universal_newlines=True, errors="replace")
        assert proc.stdout is not None
        for line in proc.stdout:
            line = line.rstrip("\n")
            log.write(line + "\n")
            tail.append(line)
            fraction = progress_fraction(line)
            if fraction:
                percent = fraction[0] * 100 // max(fraction[1], 1)
                if sys.stdout.isatty():
                    sys.stdout.write(f"\r  {fraction[0]}/{fraction[1]} ({percent}%)   ")
                    sys.stdout.flush()
                elif percent // 10 != last_percent // 10:
                    say(f"  {percent}% ({fraction[0]}/{fraction[1]})")
                last_percent = percent
        code = proc.wait()
    if sys.stdout.isatty() and last_percent >= 0:
        sys.stdout.write("\n")
    return code, list(tail)


def git_commit() -> str:
    try:
        out = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=str(ROOT), stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, universal_newlines=True, timeout=20)
        return out.stdout.strip() if out.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def fail(lines: Sequence[str]) -> int:
    say()
    say("The build failed. The last lines:")
    for line in lines[-15:]:
        say(f"  {line}")
    say()
    say("Hint: " + hint_for_failure("\n".join(lines)))
    return 1


# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="only check the prerequisites")
    parser.add_argument("--yes", "-y", action="store_true",
                        help="ask nothing: use the options given, then your remembered answers, then the defaults")
    parser.add_argument("--d3d8", metavar="PATH", help="your d3d8ntpr.lib (Aug 2001 Direct3D)")
    parser.add_argument("--oxdk", metavar="DIR", help="OXDK's checkout (default: OXDK_DIR, or ~/OXDK)")
    parser.add_argument("--players", metavar="N", help="32 (default), 16 (the Xbox's own game), or 64 for a test build")
    parser.add_argument("--release", action="store_true", help="a release build: assertions are not checked")
    parser.add_argument("--debug-tools", action="store_true", help="keep the debug decomp's developer tools")
    parser.add_argument("--asserts", action="store_true",
                        help="keep all of the debug decomp's asserts (about 370 KB more image)")
    parser.add_argument("--maps", metavar="DIR", help="your own NTSC disc's maps folder: also packages the game")
    parser.add_argument("--devkit-64mb", action="store_true",
                        help="package for a development kit limited to 64 MB (as a stock console)")
    parser.add_argument("--out", metavar="DIR", help="where to put the packaged game "
                        "(default: ./out/warthog-xbox-<date>-<commit>)")
    parser.add_argument("--no-package", action="store_true", help="build only")
    parser.add_argument("--forget", action="store_true", help="forget the remembered answers first")
    args = parser.parse_args()

    Console.interactive = not args.yes and not args.check and sys.stdin.isatty()
    Console.use_color = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None and os_name() != "windows"
    state = {} if args.forget else load_state()

    players = DEFAULT_PLAYERS
    if args.players is not None:
        players = parse_players(args.players) or 0
        if not players:
            say("--players must be 16, 32 or 64.")
            return 2
    else:
        players = parse_players(str(state.get("players", ""))) or DEFAULT_PLAYERS

    say("Warthog easy build: the original Xbox version")
    say(f"Working in {ROOT}")

    oxdk = args.oxdk or str(state.get("oxdk") or os.environ.get("OXDK_DIR") or Path.home() / "OXDK")
    d3d8 = args.d3d8 or str(state.get("d3d8") or "")

    # step 1: prerequisites
    heading("Step 1: what the build needs")
    checks = run_checks(state, oxdk, d3d8)
    # missing files you supply: ask for them before reporting
    if Console.interactive:
        if not checks_ok(checks, "OXDK"):
            say(f"OXDK is Microsoft-free toolchain glue, public at {OXDK_URL}.")
            oxdk = ask_path("Where is your OXDK folder (Enter to skip)", oxdk) or oxdk
        if not Path(d3d8).expanduser().is_file():
            say()
            say("The build needs d3d8ntpr.lib, the Aug 2001 Direct3D library from the original Xbox SDK.")
            say("It is Microsoft's file, which we can't distribute: you supply your own copy.")
            d3d8 = ask_path("Path to your d3d8ntpr.lib (Enter to skip)", d3d8)
        checks = run_checks(state, oxdk, d3d8)
    ok = print_checks(checks)
    if args.check:
        say()
        say("All set." if ok else "Some things are missing; the lines above say how to get them.")
        return 0 if ok else 1
    if not ok:
        say()
        say("Fix the items marked MISSING, then run this again. Your answers are remembered.")
        remember(state, oxdk, d3d8, players)
        return 1
    remember(state, oxdk, d3d8, players)

    # step 2: choices
    heading("Step 2: choices")
    release = args.release or bool(state.get("release", False))
    debug_tools = args.debug_tools or bool(state.get("debug_tools", False))
    asserts = args.asserts or bool(state.get("asserts", False))
    if Console.interactive:
        say("Build type: in a normal build a failed assertion stops the game and shows the")
        say("error, which helps bug reports). Release builds go on after one, as the retail game did.")
        release = ask_yes_no("Release build?", release)
        say("Asserts: by default most of the debug build's asserts are left out, as testers' builds do (the memory")
        say("checks stay). Keeping them all costs about 370 KB of memory.")
        asserts = ask_yes_no("Keep all asserts?", asserts)
        if args.players is None:
            say("Players: 32 is the default, 16 is what the Xbox's own game had. 64 is a test build that says so on screen.")
            while True:
                choice = parse_players(ask("Players (16, 32 or 64)", str(players)))
                if choice:
                    players = choice
                    break
                say("  Please enter 16, 32 or 64.")
        say("Developer tools (profiler, AI and render debug, stack walker) cost about 0.9 MB of memory.")
        debug_tools = ask_yes_no("Include developer tools?", debug_tools)
    say(f"  build: {'release' if release else 'normal'}, {players} players, all asserts "
        f"{'on' if asserts else 'off'}, developer tools {'on' if debug_tools else 'off'}")
    state.update({"release": release, "debug_tools": debug_tools, "asserts": asserts, "players": players})
    save_state(state)

    # step 3: build
    heading("Step 3: building")
    LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    LOG_FILE.write_text(f"easy_build {datetime.datetime.now().isoformat()}\n", encoding="utf-8")
    env = dict(os.environ)
    extra = [d for d in llvm_dirs() if Path(d).is_dir()]
    if extra:
        env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    code, tail = run_logged("Configuring", [sys.executable] + configure_args(
        str(Path(d3d8).expanduser().resolve()), str(Path(oxdk).expanduser().resolve()), players, release, debug_tools,
        asserts),
        env)
    if code != 0:
        return fail(tail)
    ninja = find_tool("ninja") or "ninja"
    code, tail = run_logged("Building (the first build takes a few minutes)", [ninja, "xbox"], env)
    if code != 0:
        return fail(tail)
    xbe = ROOT / "build" / "xbox" / "default.xbe"
    if not xbe.is_file():
        return fail(tail + ["build/xbox/default.xbe was not produced; configure.py found no xbox target."])
    say(f"Built {xbe} ({xbe.stat().st_size:,} bytes).")

    # step 4: package
    heading("Step 4: packaging (optional)")
    maps = args.maps or str(state.get("maps") or "")
    if args.no_package:
        maps = ""
    elif Console.interactive and not args.maps:
        say("To play it you need your own copy of the game's maps, from the North American (NTSC) disc,")
        say("build 01.10.12.2276. This script copies them next to default.xbe; it never provides them.")
        if ask_yes_no("Package the game for your console now?", True):
            maps = ask_path("Path to your maps folder", maps)
        else:
            maps = ""
    if not maps:
        say("Skipped. Later: python3 tools/easy_build.py --maps /path/to/your/maps")
        return finish(None)
    usable, notes = check_maps(Path(maps).expanduser())
    for note in notes:
        say(f"  Warning: {note}")
    if not usable:
        say("Not packaging.")
        return 1
    state["maps"] = maps
    devkit = args.devkit_64mb or bool(state.get("devkit_64mb", False))
    if Console.interactive and not args.devkit_64mb:
        say("A development kit has 128 MB; stock consoles have 64. This flag makes a devkit run the game")
        say("with 64 MB, as a stock console would (a stock console ignores it).")
        devkit = ask_yes_no("Is this for a development kit limited to 64 MB?", devkit)
    state["devkit_64mb"] = devkit
    out_default = str(Path("out") / default_out_name(datetime.date.today(), git_commit()))
    out = args.out or ask("Output folder", out_default)
    out_path = Path(out).expanduser()
    if not out_path.is_absolute():
        out_path = ROOT / out_path
    save_state(state)
    code, tail = run_logged("Packaging", [sys.executable] + package_args(
        str(Path(maps).expanduser().resolve()), str(out_path), devkit), env)
    if code != 0:
        return fail(tail)
    for line in tail[-2:]:
        say(f"  {line}")
    return finish(out_path)


def checks_ok(checks: Sequence[Check], name: str) -> bool:
    return all(c.ok for c in checks if c.name.startswith(name))


def remember(state: Dict[str, object], oxdk: str, d3d8: str, players: int) -> None:
    state["oxdk"] = oxdk
    if d3d8:
        state["d3d8"] = d3d8
    state["players"] = players
    save_state(state)


def finish(out: Optional[Path]) -> int:
    heading("Done")
    if out is None:
        say("Your build is build/xbox/default.xbe. To run it, it needs your maps beside it (see Step 4).")
        return 0
    say(f"Your game is in {out}")
    say()
    say("To run it on the console:")
    say("  1. Copy that whole folder to the Xbox's hard disk, for example to E:\\Games\\Warthog")
    say("     (FTP on a modified console; a devkit's tools work too).")
    say("  2. Start default.xbe from the dashboard.")
    say("  3. Settings are in config.toml beside default.xbe (join, trace, memory_class and the others);")
    say("     the first start writes every setting, commented out, with its help. Restart after editing.")
    say("  4. The game writes its log to debug.txt in the same folder: attach it to bug reports.")
    say("  5. Questions and reports: the #warthog-support forum on the Discord (discord.gg/chupathingy).")
    say("The folder holds your own maps: don't share it.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        say("\nStopped.")
        sys.exit(130)
