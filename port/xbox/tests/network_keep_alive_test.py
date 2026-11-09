"""The console's keep-alive layout (network version 31) and message 90 (32),
as the wire format record (port/linux/network_format.txt) and the console's
packet table (source/networking/network_messages.c) have them: the pregame
keep-alive is 4 bytes, the postgame's 2, each with a field list of its own;
the aim assist policy is message 90 (12 bytes) and the console's game version
is announced as 32."""
import re
import sys
from pathlib import Path

# the game (the submodule, chupathingyce/, or the directory given)
root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[3] / "chupathingyce"
record = (root / "port/linux/network_format.txt").read_text()
table = (root / "source/networking/network_messages.c").read_text()
limits = (root / "port/linux/include/halo_port_limits.h").read_text()
delta = (root / "port/linux/include/delta.h").read_text()
failures = []


def check(condition, text):
    if not condition:
        failures.append(text)


check("size message_server_pregame_keep_alive 0x04" in record, "pregame keep-alive is 4 bytes")
check("size message_server_postgame_keep_alive 0x02" in record, "postgame keep-alive is 2 bytes")
check(re.search(r"^fields server_pregame_keep_alive \w+$", record, re.M) is not None, "pregame fields recorded")
check(re.search(r"^fields server_postgame_keep_alive \w+$", record, re.M) is not None, "postgame fields recorded")
# the packet table: each keep-alive has its own field list, the pregame's a
# long on the wire (4 bytes, the last two 0), the postgame's a short
check("server_pregame_keep_alive_fields" in table and "server_postgame_keep_alive_fields" in table,
      "each keep-alive lists its own fields")
match = re.search(r"DATA_PACKET_FIELD\(_data_packet_field_longs, 1\),\s*DATA_PACKET_FIELD_END,\s*\},\s*"
                  r"NETWORK_GAME_MESSAGE_DEFINITION\(server_pregame_keep_alive", table)
check(match is not None, "the pregame keep-alive is one long")
match = re.search(r"DATA_PACKET_FIELD\(_data_packet_field_shorts, 1\),\s*DATA_PACKET_FIELD_END,\s*\},\s*"
                  r"NETWORK_GAME_MESSAGE_DEFINITION\(server_postgame_keep_alive", table)
check(match is not None, "the postgame keep-alive is one short")
check(re.search(r"^\s*(enum )?.*aim_assist.* 90|message_aim_assist_policy|_distributed_message_aim_assist_policy",
                (root / "port/linux/game/network_distributed.c").read_text(), re.M) is not None,
      "message 90 is the aim assist policy")
check(re.search(r"#define HALO_PORT_NETWORK_VERSION 32\b", limits) is not None, "announces 32")
check(re.search(r'#define DELTA_WIRE "chupa-\d+[a-z]"', delta) is not None, "the game names a Delta wire")
if failures:
    print("\n".join("FAIL: " + f for f in failures))
    sys.exit(1)
print("network_keep_alive_test: ok")
