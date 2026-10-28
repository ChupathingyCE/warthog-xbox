/*
NETWORK_GAME_LAYOUT_TEST.C

The game settings record between the PC builds' and the console's layouts
(port/linux/game/network_game_layout.c) on the host: the sizes the game
asserts, a round trip, and every reason a fold refuses; and the game type
states' fold (slayer's, as game_engine_slayer.c lays it out).
port/xbox/tests/run.sh.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/linux/game/network_game_layout.h"

#define PC NETWORK_GAME_LAYOUT_PC_SLOTS
/* the console's slots: 16 players, and 17 machines (a dedicated host is
machine #0, halo_port_limits.h) */
#define CONSOLE_P NETWORK_GAME_LAYOUT_CONSOLE_SLOTS
#define CONSOLE_M (CONSOLE_P + 1)

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; \
	fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static unsigned char *machine(unsigned char *record, int slots, int index)
{
	(void)slots;
	return record + NETWORK_GAME_LAYOUT_MACHINES_OFFSET + index * NETWORK_GAME_LAYOUT_MACHINE_SIZE;
}

static unsigned char *player_count(unsigned char *record, int slots)
{
	return record + NETWORK_GAME_LAYOUT_MACHINES_OFFSET + slots * NETWORK_GAME_LAYOUT_MACHINE_SIZE;
}

static unsigned char *player(unsigned char *record, int slots, int index)
{
	return player_count(record, slots) + 2 + index * NETWORK_GAME_LAYOUT_PLAYER_SIZE;
}

/* a PC host's record: the header and tail filled, machines in slots 0 to
machines - 1, players in slots 0 to players - 1 (FFA: each its own team),
the rest empty as the game leaves them */
static unsigned char *pc_record(int machines, int players)
{
	unsigned long size = network_game_layout_size(PC, PC);
	unsigned char *record = calloc(1, size);
	unsigned long index;
	int slot;

	if (!record)
		abort();
	for (index = 0; index < NETWORK_GAME_LAYOUT_MACHINES_OFFSET; index++)
		record[index] = (unsigned char)(index * 7 + 3);
	record[NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET] = 16;
	record[NETWORK_GAME_LAYOUT_MACHINE_COUNT_OFFSET] = (unsigned char)machines;
	record[NETWORK_GAME_LAYOUT_MACHINE_COUNT_OFFSET + 1] = 0;
	for (slot = 0; slot < PC; slot++)
	{
		unsigned char *m = machine(record, PC, slot);
		unsigned char *p = player(record, PC, slot);

		m[NETWORK_GAME_LAYOUT_MACHINE_INDEX] = 0xFF;
		memset(p + NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX, 0xFF, 4);
		if (slot < machines)
		{
			m[0] = (unsigned char)('A' + slot);
			m[NETWORK_GAME_LAYOUT_MACHINE_INDEX] = (unsigned char)slot;
		}
		if (slot < players)
		{
			p[0] = (unsigned char)('a' + slot);
			p[0x18] = (unsigned char)slot;
			p[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = (unsigned char)(slot % machines);
			p[NETWORK_GAME_LAYOUT_PLAYER_CONTROLLER_INDEX] = 0;
			p[NETWORK_GAME_LAYOUT_PLAYER_TEAM_INDEX] = (unsigned char)slot;
			p[NETWORK_GAME_LAYOUT_PLAYER_LIST_INDEX] = (unsigned char)slot;
		}
	}
	player_count(record, PC)[0] = (unsigned char)players;
	for (index = size - NETWORK_GAME_LAYOUT_TAIL_SIZE; index < size; index++)
		record[index] = (unsigned char)(index * 13 + 1);
	return record;
}

static int fold(unsigned char *record, unsigned char *console)
{
	return network_game_layout_convert(record, PC, PC, console, CONSOLE_M, CONSOLE_P, NULL);
}

int main(void)
{
	unsigned long pc_size = network_game_layout_size(PC, PC);
	unsigned long console_size = network_game_layout_size(CONSOLE_M, CONSOLE_P);
	unsigned char *record, *console, *back;
	int clamped;

	/* (halo_port_limits.h's HALO_PORT_NETWORK_GAME_SIZE at each build's limits) */
	CHECK(pc_size == 13120);
	CHECK(console_size == 0x780 + 0x44);
	/* (the retail game's 0x434 at 4 and 16, and version 11's 0x1C bytes of PC options) */
	CHECK(network_game_layout_size(4, 16) == 0x434 + 0x1C);

	console = malloc(console_size);
	back = malloc(pc_size);
	if (!console || !back)
		abort();

	/* a round trip, exact */
	record = pc_record(5, 16);
	CHECK(network_game_layout_convert(record, PC, PC, console, CONSOLE_M, CONSOLE_P, &clamped) == NETWORK_GAME_LAYOUT_OK);
	CHECK(!clamped);
	CHECK(player(console, CONSOLE_M, 15)[0] == 'p' && player(console, CONSOLE_M, 15)[NETWORK_GAME_LAYOUT_PLAYER_LIST_INDEX] == 15);
	CHECK(machine(console, CONSOLE_M, 4)[0] == 'E' && machine(console, CONSOLE_M, 5)[NETWORK_GAME_LAYOUT_MACHINE_INDEX] == 0xFF);
	CHECK(!memcmp(console + console_size - NETWORK_GAME_LAYOUT_TAIL_SIZE, record + pc_size - NETWORK_GAME_LAYOUT_TAIL_SIZE,
		NETWORK_GAME_LAYOUT_TAIL_SIZE));
	CHECK(network_game_layout_convert(console, CONSOLE_M, CONSOLE_P, back, PC, PC, &clamped) == NETWORK_GAME_LAYOUT_OK);
	CHECK(!memcmp(back, record, pc_size));
	free(record);

	/* the gametype's maximum held to the console's slots */
	record = pc_record(2, 2);
	record[NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET] = 128;
	CHECK(network_game_layout_convert(record, PC, PC, console, CONSOLE_M, CONSOLE_P, &clamped) == NETWORK_GAME_LAYOUT_OK);
	CHECK(clamped && console[NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET] == CONSOLE_P);
	free(record);

	/* a dedicated host and 16 players: 17 machines, machine #16 in the last slot */
	record = pc_record(17, 16);
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_OK);
	CHECK(machine(console, CONSOLE_M, 16)[0] == 'Q');
	CHECK(player(console, CONSOLE_M, 15)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] == 15);
	free(record);

	/* the host's direction: this build's own record widened to the network's
	and folded back, exact (a console host with 16 players and its dedicated slot) */
	record = pc_record(17, 16);
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_OK);
	free(record);
	record = malloc(pc_size);
	if (!record)
		abort();
	CHECK(network_game_layout_convert(console, CONSOLE_M, CONSOLE_P, record, PC, PC, &clamped) == NETWORK_GAME_LAYOUT_OK);
	CHECK(!clamped);
	CHECK(network_game_layout_convert(record, PC, PC, back, CONSOLE_M, CONSOLE_P, &clamped) == NETWORK_GAME_LAYOUT_OK);
	CHECK(!clamped && !memcmp(back, console, console_size));
	free(record);

	/* refusals */
	record = pc_record(18, 2);
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_TOO_MANY_MACHINES);
	free(record);
	record = pc_record(4, 17);
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_TOO_MANY_PLAYERS);
	free(record);
	/* (a machine in slot 20: a PC host numbers machines by connection slot) */
	record = pc_record(3, 3);
	machine(record, PC, 20)[NETWORK_GAME_LAYOUT_MACHINE_INDEX] = 20;
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_MACHINE_SLOT);
	free(record);
	/* (a player in slot 40, after others quit mid-game) */
	record = pc_record(3, 3);
	memcpy(player(record, PC, 40), player(record, PC, 2), NETWORK_GAME_LAYOUT_PLAYER_SIZE);
	player(record, PC, 40)[NETWORK_GAME_LAYOUT_PLAYER_LIST_INDEX] = 40;
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_PLAYER_SLOT);
	free(record);
	/* (a used player naming a machine past 16 that is in the game) */
	record = pc_record(3, 3);
	machine(record, PC, 30)[NETWORK_GAME_LAYOUT_MACHINE_INDEX] = 30;
	player(record, PC, 1)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 30;
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_MACHINE_SLOT);
	free(record);
	/* (players of no machine past 16, as a host's bots, from #113 up: given
	the free indices from the top, the same one for each of a machine's) */
	record = pc_record(3, 6);
	player(record, PC, 0)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 113;
	player(record, PC, 1)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 114;
	player(record, PC, 4)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 113;
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_OK);
	CHECK(player(console, CONSOLE_M, 0)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] == 16);
	CHECK(player(console, CONSOLE_M, 1)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] == 15);
	CHECK(player(console, CONSOLE_M, 4)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] == 16);
	CHECK(player(console, CONSOLE_M, 2)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] == 2);
	CHECK(machine(console, CONSOLE_M, 16)[NETWORK_GAME_LAYOUT_MACHINE_INDEX] == 0xFF);
	free(record);
	/* (... each keeping its index from one record to the next, while it is
	free, and a player of such a machine joining in progress given it too) */
	{
		struct network_game_layout_machine_map kept;
		unsigned char used[CONSOLE_M];

		network_game_layout_machine_map_reset(&kept);
		record = pc_record(3, 4);
		player(record, PC, 0)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 114;
		CHECK(network_game_layout_convert_mapped(record, PC, PC, console, CONSOLE_M, CONSOLE_P, NULL, &kept) ==
			NETWORK_GAME_LAYOUT_OK);
		CHECK(player(console, CONSOLE_M, 0)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] == 16);
		/* (machine #113's player comes first now: #114's keeps 16) */
		player(record, PC, 3)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 113;
		memcpy(player(record, PC, 3), player(record, PC, 0), NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX);
		player(record, PC, 0)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 113;
		player(record, PC, 3)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 114;
		CHECK(network_game_layout_convert_mapped(record, PC, PC, console, CONSOLE_M, CONSOLE_P, NULL, &kept) ==
			NETWORK_GAME_LAYOUT_OK);
		CHECK(player(console, CONSOLE_M, 3)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] == 16);
		CHECK(player(console, CONSOLE_M, 0)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] == 15);
		free(record);
		memset(used, 0, sizeof(used));
		used[0] = used[1] = used[2] = used[15] = used[16] = 1;
		CHECK(network_game_layout_machine_map_index(&kept, 114, used, CONSOLE_M) == 14);
		used[14] = 1;
		CHECK(network_game_layout_machine_map_index(&kept, 114, used, CONSOLE_M) == 13);
		CHECK(network_game_layout_machine_map_index(&kept, 2, used, CONSOLE_M) == 2);
		memset(used, 1, sizeof(used));
		CHECK(network_game_layout_machine_map_index(&kept, 120, used, CONSOLE_M) == -1);
	}
	/* (... with no index free: every one a machine's) */
	record = pc_record(17, 3);
	player(record, PC, 1)[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX] = 113;
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_INDEX);
	free(record);
	/* (a used player naming a team past 16) */
	record = pc_record(3, 3);
	player(record, PC, 1)[NETWORK_GAME_LAYOUT_PLAYER_TEAM_INDEX] = 99;
	CHECK(fold(record, console) == NETWORK_GAME_LAYOUT_INDEX);
	free(record);
	/* (bad slot counts) */
	record = pc_record(1, 1);
	CHECK(network_game_layout_convert(record, PC, PC, console, 0, CONSOLE_P, NULL) == NETWORK_GAME_LAYOUT_BAD_SIZE);
	CHECK(network_game_layout_convert(record, 129, PC, console, CONSOLE_M, CONSOLE_P, NULL) == NETWORK_GAME_LAYOUT_BAD_SIZE);
	free(record);
	CHECK(!strcmp(network_game_layout_error_string(99), "unknown error"));

	/* a PC host's slayer state into the console's: team and individual
	scores (longs), targets (bytes, 0xFF none) and speeds (words), each one a
	slot; the sizes are docs/cross-play.md's 1,408 and 176 */
	{
		static const struct network_game_layout_field slayer[] =
		{
			{ 4, 1, 0 }, { 4, 1, 0 }, { 1, 1, 0xFF }, { 2, 1, 0 },
		};
		static const struct network_game_layout_field mixed[] =
		{
			{ 4, 0, 0 }, { 2, 1, 0 }, { 1, 0, 0 },
		};
		unsigned char wire[1408], local[176], wide[1408];
		int dropped, slot;

		CHECK(network_game_layout_fields_size(slayer, 4, PC) == 1408);
		CHECK(network_game_layout_fields_size(slayer, 4, CONSOLE_P) == 176);
		memset(wire, 0, sizeof(wire));
		memset(wire + 1024, 0xFF, 128);
		for (slot = 0; slot < CONSOLE_P; slot++)
		{
			wire[slot * 4] = (unsigned char)(slot + 1);
			wire[512 + slot * 4 + 1] = (unsigned char)(slot + 2);
			wire[1024 + slot] = (unsigned char)((slot + 1) % CONSOLE_P);
			wire[1152 + slot * 2] = 0x40;
		}
		CHECK(network_game_layout_fold_fields(wire, sizeof(wire), PC, local, sizeof(local), CONSOLE_P, slayer, 4,
			&dropped) == NETWORK_GAME_LAYOUT_OK);
		CHECK(dropped == 0);
		CHECK(local[15 * 4] == 16 && local[64 + 15 * 4 + 1] == 17 && local[128 + 15] == 0 && local[144 + 30] == 0x40);
		/* (and back: the PCs' layout exactly) */
		CHECK(network_game_layout_fold_fields(local, sizeof(local), CONSOLE_P, wide, sizeof(wide), PC, slayer, 4,
			NULL) == NETWORK_GAME_LAYOUT_OK);
		CHECK(!memcmp(wide, wire, sizeof(wire)));
		/* a score, a target and a speed past slot 16: dropped and counted */
		wire[20 * 4] = 3;
		wire[1024 + 40] = 2;
		wire[1152 + 100 * 2 + 1] = 1;
		CHECK(network_game_layout_fold_fields(wire, sizeof(wire), PC, local, sizeof(local), CONSOLE_P, slayer, 4,
			&dropped) == NETWORK_GAME_LAYOUT_OK);
		CHECK(dropped == 3);
		/* sizes that are not the layouts' */
		CHECK(network_game_layout_fold_fields(wire, sizeof(wire) - 1, PC, local, sizeof(local), CONSOLE_P, slayer, 4,
			NULL) == NETWORK_GAME_LAYOUT_BAD_SIZE);
		CHECK(network_game_layout_fold_fields(wire, sizeof(wire), PC, local, sizeof(local) + 1, CONSOLE_P, slayer, 4,
			NULL) == NETWORK_GAME_LAYOUT_BAD_SIZE);
		CHECK(network_game_layout_fold_fields(local, sizeof(local), CONSOLE_P, wire, sizeof(wire), PC, slayer, 4,
			NULL) == NETWORK_GAME_LAYOUT_OK);
		/* fields of one element are copied whole */
		CHECK(network_game_layout_fields_size(mixed, 3, PC) == 4 + 256 + 1);
		{
			unsigned char from[261], to[37];

			memset(from, 0, sizeof(from));
			from[0] = 9;
			from[4] = 7;
			from[260] = 5;
			CHECK(network_game_layout_fold_fields(from, sizeof(from), PC, to, sizeof(to), CONSOLE_P, mixed, 3, &dropped) ==
				NETWORK_GAME_LAYOUT_OK);
			CHECK(to[0] == 9 && to[4] == 7 && to[36] == 5 && dropped == 0);
		}
	}

	free(console);
	free(back);
	if (failures)
	{
		fprintf(stderr, "network_game_layout_test: %d failed\n", failures);
		return 1;
	}
	printf("network_game_layout_test: ok\n");
	return 0;
}
