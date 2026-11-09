#!/bin/sh
# Checks of the Xbox build's own units (port/xbox/tests/run.sh --xbox): what
# the console's C runtime and SDK headers would get wrong, before a console
# finds it.
#
# - va_start: a unit compiled with the SDK's headers first (port/xbox/src,
#   internet play's) must expand va_start to clang's builtin
#   (port/xbox/include/xbox_stdarg.h), never the SDK's "&last + size", which
#   reads the wrong memory once clang inlines a variadic function.
# - printf formats: the console's C runtime (2003's) has no %z, %ll, %j or
#   %hh length modifiers; the units it formats with must not use them.
#   (Formats against their arguments are -Werror=format in the build.)
# - the image's base: the build links with /fixed (no .reloc), so cxbe must
#   place the PE at the base it was linked for (0x10000). If the XBE's
#   headers grew past 4 KB, cxbe would move it, and nothing would fix the
#   addresses. Checked when build/xbox/default.xbe is there.
#
# Needs a configured Xbox build (build.ninja with the xbox target).
set -e
cd "$(dirname "$0")/../../.."
G=${GAME:-chupathingyce}
failures=0
probe=build/xbox-tests/va_probe.c
mkdir -p build/xbox-tests
printf '#include <stdarg.h>\nvoid probe(const char *f, ...) { va_list a; va_start(a, f); va_end(a); }\n' > "$probe"
command=$(ninja -t commands build/xbox/obj/port/src/xbox_port.c.obj | tail -1)
if [ -z "$command" ]; then
	echo "xbox_check: no Xbox build configured (chupathingyce/configure.py --console-port port/xbox --xbox-d3d8 LIBRARY)" >&2
	exit 2
fi
command=$(printf '%s' "$command" | sed -e 's| -c port/xbox/src/xbox_port.c| -E '"$probe"'|' -e 's| -o [^ ]*\.obj| -o -|' \
	-e 's|-MD -MF [^ ]* ||')
expanded=$(eval "$command" | grep -A1 'void probe' || true)
if printf '%s' "$expanded" | grep -q '__builtin_va_start'; then
	echo "xbox_check: va_start is clang's builtin"
else
	echo "xbox_check: va_start is not clang's builtin in the SDK-header units:" >&2
	printf '%s\n' "$expanded" >&2
	failures=$((failures + 1))
fi
# (format strings with a length modifier the 2003 runtime lacks)
bad=$(grep -nE '"[^"]*%[-+ #0-9.*]*(z|ll|j|hh|t)[diouxXcs]' $G/port/linux/src/p2p.c $G/port/linux/src/p2p_signal.c \
	$G/port/linux/src/p2p_crypto.c $G/port/linux/src/port_config.c $G/port/linux/src/delta_wire.c $G/port/linux/src/delta_peer.c $G/port/linux/src/delta_chat.c $G/port/linux/src/delta_peer_game.c port/xbox/src/*.c port/xbox/game/*.c \
	$G/port/linux/game/network_*.c || true)
if [ -n "$bad" ]; then
	echo "xbox_check: formats the console's C runtime does not read:" >&2
	printf '%s\n' "$bad" >&2
	failures=$((failures + 1))
else
	echo "xbox_check: no %z, %ll, %j, %hh or %t formats"
fi
if [ -f build/xbox/default.xbe ]; then
	# (the XBE header's PE base, 0x13C, against the base build.py links for)
	if python3 -c 'import struct, sys; d = open(sys.argv[1], "rb").read(0x140); sys.exit(struct.unpack_from("<I", d, 0x13C)[0] != 0x10000)' \
		build/xbox/default.xbe; then
		echo "xbox_check: the XBE's image sits at its link base"
	else
		echo "xbox_check: cxbe moved the image off its link base (0x10000), and the build has no relocations" >&2
		failures=$((failures + 1))
	fi
fi
[ "$failures" -eq 0 ]
