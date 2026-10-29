#!/bin/sh
# The console's game list code on the host (port/xbox/src/xbox_game_list_*.c),
# under AddressSanitizer and UndefinedBehaviorSanitizer:
#
#   port/xbox/tests/run.sh               the parsers', the screen's words', the
#                                        settings record and game type state
#                                        layouts', console.join's and the memory
#                                        class's, XLink's, the log tags' and the
#                                        co-op names' tests, which hosts it
#                                        joins, tools/xbox_package.py's
#                                        devkit flag,
#                                        the console's config.toml, and 200,000
#                                        mutations
#   port/xbox/tests/run.sh --fuzz 300    and libFuzzer for 300 seconds (LLVM's clang)
#   port/xbox/tests/run.sh --fetch       and the fetch of the real list, over the
#                                        host's sockets (warthog.milenko.org)
#   port/xbox/tests/run.sh --xbox        and the Xbox build's own checks
#                                        (xbox_check.sh: va_start, formats)
#
# CC picks the compiler (default: Homebrew's clang if there, else cc). The
# binaries and libFuzzer's corpus go to build/xbox-tests. Run from this
# repository, with the game in chupathingyce/ (git submodule update --init;
# GAME names another checkout of it).
set -e
cd "$(dirname "$0")/../../.."
G=${GAME:-chupathingyce}
if [ ! -f "$G/port/linux/src/port_config.c" ]; then
	echo "$0: the game is not in $G (git submodule update --init, or GAME=DIR)" >&2
	exit 2
fi
if [ -z "$CC" ]; then
	CC=cc
	[ -x /opt/homebrew/opt/llvm/bin/clang ] && CC=/opt/homebrew/opt/llvm/bin/clang
fi
OUT=build/xbox-tests
SAN="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined"
SRC=port/xbox/src
mkdir -p "$OUT"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN -c $SRC/xbox_game_list_parse.c -o "$OUT/parse.o"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN -c $SRC/xbox_game_list_text.c -o "$OUT/text.o"
$CC -Wall -Wextra $SAN port/xbox/tests/game_list_test.c "$OUT/parse.o" "$OUT/text.o" -o "$OUT/game_list_test"
"$OUT/game_list_test" 200000
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN -iquote "$G" port/xbox/tests/network_game_layout_test.c \
	$G/port/linux/game/network_game_layout.c -o "$OUT/network_game_layout_test"
$CC -std=c99 -pedantic -Wall -Wextra -Werror $SAN -iquote "$G" port/xbox/tests/network_rules_console_test.c \
	$G/port/linux/game/network_rules.c -o "$OUT/network_rules_console_test"
"$OUT/network_rules_console_test"
python3 port/xbox/tests/network_keep_alive_test.py "$G"
"$OUT/network_game_layout_test"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN port/xbox/tests/direct_join_test.c $SRC/xbox_direct_join.c \
	-o "$OUT/direct_join_test"
"$OUT/direct_join_test"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN port/xbox/tests/memory_test.c $SRC/xbox_memory.c \
	-o "$OUT/memory_test"
"$OUT/memory_test"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN port/xbox/tests/xlink_test.c $SRC/xbox_xlink.c -o "$OUT/xlink_test"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN port/xbox/tests/log_tag_test.c $SRC/xbox_log_tag.c \
	-o "$OUT/log_tag_test"
"$OUT/log_tag_test"
"$OUT/xlink_test"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN port/xbox/tests/coop_name_test.c $SRC/xbox_coop_name.c \
	-o "$OUT/coop_name_test"
"$OUT/coop_name_test"
python3 port/xbox/tests/package_test.py
python3 port/xbox/tests/easy_build_test.py
# D:\config.toml: port_config.c as the console builds it, beside a stand-in
# for its platform.h, run in a folder where "d:\config.toml" is a name
rm -rf "$OUT/config"
mkdir -p "$OUT/config/run"
cp $G/port/linux/src/port_config.c "$OUT/config/port_config.c"
cp port/xbox/tests/config_platform.h "$OUT/config/platform.h"
cp $G/port/linux/include/halo_port_limits.h $G/port/linux/include/halo_port_capacity.h \
	$G/port/linux/include/halo_port_features.h $G/port/linux/include/halo_console_memory.h \
	$G/port/linux/include/halo_console_features.h "$OUT/config/"
# (the parser itself without the sanitizers: its offsetof by a null pointer
# is undefined behavior they report, which every compiler takes as meant)
$CC -std=gnu11 -O1 -w -c $G/port/third_party/tomlc17/tomlc17.c -o "$OUT/config/tomlc17.o"
# (with what the console leaves out, as port/xbox/build.py builds it)
$CC -std=gnu11 -Wall -Wextra -Werror $SAN -DHALO_XBOX_CONSOLE -DHALO_CONSOLE_PLAYERS=16 \
	-include port/xbox/include/halo_xbox_features.h \
	-I$G/port/linux/src -I$G/port/third_party/tomlc17 \
	port/xbox/tests/config_test.c "$OUT/config/port_config.c" "$OUT/config/tomlc17.o" -o "$OUT/config/config_test"
(cd "$OUT/config/run" && ../config_test new && ../config_test given)
# the console's player identity (D:\game_list_player.key)
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN -c $SRC/xbox_player_key.c -o "$OUT/player_key.o"
$CC -std=gnu11 -Wall -Wextra -Werror $SAN port/xbox/tests/player_key_test.c "$OUT/player_key.o" \
	-o "$OUT/player_key_test"
(cd "$OUT" && ./player_key_test)
# the console's Delta Peer client (HALO_XBOX_CONSOLE, at the Xbox's 16 players: its
# 17 machines, no table room)
$CC -std=gnu11 -Wall -Wextra -Werror $SAN -DHALO_XBOX_CONSOLE -DHALO_CONSOLE_PLAYERS=16 -DHALO_CONSOLE_DELTA \
	-I$G/port/linux/src -I$G/port/third_party/monocypher \
	-idirafter $G/port/linux/include port/xbox/tests/delta_console_test.c $G/port/linux/src/delta_peer.c $G/port/linux/src/delta_chat.c \
	$G/port/linux/src/delta_wire.c $G/port/third_party/monocypher/monocypher.c \
	$G/port/third_party/monocypher/monocypher-ed25519.c -o "$OUT/delta_console_test"
"$OUT/delta_console_test"
# which hosts the console joins: a Delta host by its wire and range, whatever
# number it announces (delta_compat.c as the console builds it), and the
# game's join check on the console's gates
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN -DHALO_XBOX_CONSOLE -DHALO_CONSOLE_PLAYERS=16 -DHALO_CONSOLE_DELTA \
	-I$G/port/linux/src -idirafter $G/port/linux/include port/xbox/tests/delta_join_test.c \
	$G/port/linux/src/delta_compat.c -o "$OUT/delta_join_test"
"$OUT/delta_join_test"
grep -q '^#if defined(HALO_GAME_BROWSER) || defined(HALO_CONSOLE_DELTA) || defined(HALO_CONSOLE_ONLINE_GAMES)$' \
	$G/source/networking/network_client_manager.c || {
	echo "$0: the game's join check does not take the Delta wire path on the console" >&2
	exit 1
}
# the console's signed platform claim (p2p_crypto.c as the console builds it,
# beside empty stand-ins for the platform layer's headers)
rm -rf "$OUT/claim"
mkdir -p "$OUT/claim"
cp $G/port/linux/src/p2p_crypto.c "$OUT/claim/p2p_crypto.c"
printf 'void platform_log(const char *format, ...);\n' > "$OUT/claim/platform.h"
printf 'void posix_random_bytes(void *bytes, unsigned long size);\n' > "$OUT/claim/posix.h"
$CC -std=gnu11 -O1 -w -I$G/port/linux/src -I$G/port/third_party/monocypher -c "$OUT/claim/p2p_crypto.c" -o "$OUT/claim/p2p_crypto.o"
$CC -std=gnu11 -Wall -Wextra -Werror $SAN -I$G/port/third_party/monocypher port/xbox/tests/claim_test.c \
	$SRC/xbox_claim.c $SRC/xbox_player_key.c "$OUT/claim/p2p_crypto.o" $G/port/third_party/monocypher/monocypher.c \
	$G/port/third_party/monocypher/monocypher-ed25519.c -o "$OUT/claim_test"
"$OUT/claim_test"
while [ $# -gt 0 ]; do
	case "$1" in
	--fuzz)
		mkdir -p "$OUT/corpus"
		printf 'HTTP/1.0 200 OK\r\nContent-Length: 21\r\n\r\nwarthog-list 1 0\nend\n' > "$OUT/corpus/seed"
		$CC -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
			port/xbox/tests/game_list_fuzz.c $SRC/xbox_game_list_parse.c -o "$OUT/game_list_fuzz"
		"$OUT/game_list_fuzz" -max_len=65536 -max_total_time="$2" -print_final_stats=1 "$OUT/corpus"
		shift 2
		;;
	--fetch)
		$CC -Wall -Wextra $SAN port/xbox/tests/game_list_fetch_test.c $SRC/xbox_game_list_fetch.c \
			$SRC/xbox_game_list_parse.c -o "$OUT/game_list_fetch_test"
		"$OUT/game_list_fetch_test"
		shift
		;;
	--xbox)
		sh port/xbox/tests/xbox_check.sh
		shift
		;;
	*)
		echo "usage: $0 [--fuzz SECONDS] [--fetch] [--xbox]" >&2
		exit 2
		;;
	esac
done
