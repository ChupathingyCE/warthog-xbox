/*
DELTA_CONSOLE_TEST.C

The console's Delta Peer client (port/linux/src/delta_peer.c built with
HALO_XBOX_CONSOLE, as port/xbox/p2p builds it): a session set up as
delta_peer_game.c sets up the console's (platform xbox, the platform
capability alone, no signed legacy table hooks) joins a host in the same
process, datagrams passed through memory. What it checks:

- the host learns the platform from HELLO: xbox, the memory class, the
  platform capability and no more;
- the host learns the player ID of a client that was given an identity
  (PROFILE), and none of an anonymous one;
- the console says it takes no legacy table, so a host that has a newer
  one sends it none (and does, when the console says nothing);
- the client ends up in a Delta session, with the room's limit of players;
- a host that never answers leaves the client in legacy, no error;
- 200,000 random datagrams to the client (some with Delta's header) do
  no harm: the console keeps no room for a relayed legacy table, and its
  session sets none up.
*/

#include "delta_peer.h"
#include "halo_port_limits.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...)
{
	(void)format;
}

#define CLIENT_IP 0x0100007Fu
#define HOST_IP 0x0200007Fu

struct datagram
{
	delta_u32 to;
	unsigned char data[DELTA_WIRE_MAXIMUM_DATAGRAM];
	int size;
};

static int tables_sent;

static struct datagram queue[64];
static int queued;
static int from_client;

static void send_datagram(void *context, delta_u32 ipv4, unsigned short port, const unsigned char *data, int size)
{
	struct delta_wire_header header;

	(void)port;
	(void)context;
	if (ipv4 == CLIENT_IP && delta_wire_read_header(data, size, &header) && header.type == _delta_message_table)
		tables_sent++;
	if (queued < 64 && size <= DELTA_WIRE_MAXIMUM_DATAGRAM)
	{
		queue[queued].to = ipv4;
		memcpy(queue[queued].data, data, (size_t)size);
		queue[queued].size = size;
		queued++;
	}
}

static void log_line(void *context, const char *text)
{
	(void)context;
	printf("  %s\n", text);
}

static delta_u32 random_number(void *context)
{
	(void)context;
	return (delta_u32)rand() << 16 ^ (delta_u32)rand();
}

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAILED: %s (line %d)\n", #condition, __LINE__); failures++; } } while (0)

/* the host's legacy table: serial 5, one byte (never taken by the console) */
static delta_u32 host_table_serial(void *context)
{
	(void)context;
	return 5;
}

static int host_table_signed(void *context, unsigned char *buffer, int size)
{
	(void)context;
	/* (this build's table room is one byte: the console relays none) */
	if (size < 1)
		return 0;
	buffer[0] = 0x5A;
	return 1;
}

static int host_table_offer(void *context, const unsigned char *table, int size)
{
	(void)context;
	(void)table;
	(void)size;
	return 0;
}

/* what the console says it takes: none */
static delta_u32 console_table_serial(void *context)
{
	(void)context;
	return DELTA_WIRE_TABLE_NONE;
}

static const unsigned char test_player_id[DELTA_WIRE_PLAYER_ID_SIZE] =
	{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

static void console_local(struct delta_peer_local *local)
{
	memset(local, 0, sizeof(*local));
	local->capabilities = (delta_u32)1 << _delta_capability_platform;
	local->legacy_version = HALO_PORT_NETWORK_VERSION;
	delta_platform_policy_default(_delta_platform_xbox, &local->key);
	local->key.platform = _delta_platform_xbox;
	local->key.memory_class = 6;
	/* (as delta_peer_local_key does: a build joins no more than its tables
	hold, whatever its platform's row allows) */
	if (local->key.join_players > HALO_CONSOLE_PLAYERS)
		local->key.join_players = (unsigned char)HALO_CONSOLE_PLAYERS;
	local->key.version = DELTA_PLATFORM_KEY_VERSION;
	snprintf(local->build, sizeof(local->build), "Warthog test");
}

/* a console given an identity (delta_peer_game.c: the profile capability
and the ID) */
static void console_local_identified(struct delta_peer_local *local)
{
	console_local(local);
	local->capabilities |= (delta_u32)1 << _delta_capability_profile;
	local->has_profile = 1;
	memcpy(local->profile.player_id, test_player_id, sizeof(test_player_id));
}

static void deliver(struct delta_peer *peer, delta_u32 address, delta_u32 from, delta_u32 now);

/* a handshake of a client and a host (a desktop's, with a legacy table
newer than the client's) for 15 seconds */
static void handshake(struct delta_peer *client, struct delta_peer *host, delta_u32 start)
{
	struct delta_peer_game_machine machines[2];
	signed char seats[DELTA_PEER_MAXIMUM_PLAYERS];
	delta_u32 now;
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_PLAYERS; index++)
		seats[index] = (signed char)(index < 2 ? index : -1);
	memset(machines, 0, sizeof(machines));
	machines[0].machine_index = 0;
	machines[0].local = 1;
	machines[1].machine_index = 1;
	machines[1].ipv4 = CLIENT_IP;
	queued = 0;
	for (now = start; now < start + 15000; now += 100)
	{
		delta_peer_client_frame(client, now, 1, HOST_IP, 4000, 1, 1, seats);
		deliver(host, HOST_IP, CLIENT_IP, now);
		delta_peer_host_frame(host, now, machines, 2, seats);
		deliver(client, CLIENT_IP, HOST_IP, now);
	}
}

/* the datagrams the other side queued, taken by one peer */
static void deliver(struct delta_peer *peer, delta_u32 address, delta_u32 from, delta_u32 now)
{
	struct datagram taken[64];
	int count = queued, index;

	memcpy(taken, queue, sizeof(struct datagram) * (size_t)count);
	queued = 0;
	for (index = 0; index < count; index++)
		if (taken[index].to == address)
			delta_peer_receive(peer, now, from, 4000, taken[index].data, taken[index].size);
}

static signed char players[DELTA_PEER_MAXIMUM_PLAYERS];

int main(void)
{
	static struct delta_peer client, host;
	struct delta_peer_env env;
	struct delta_peer_local local;
	struct delta_peer_game_machine machines[2];
	struct delta_peer_machine other;
	delta_u32 now;
	int index;

	for (index = 0; index < DELTA_PEER_MAXIMUM_PLAYERS; index++)
		players[index] = (signed char)(index < 2 ? index : -1);
	memset(&env, 0, sizeof(env));
	env.send_datagram = send_datagram;
	env.log_line = log_line;
	env.random_number = random_number;
	console_local(&local);
	delta_peer_initialize(&client, &env, &local);
	/* the host: a desktop machine's key and capabilities */
	memset(&local, 0, sizeof(local));
	local.capabilities = (delta_u32)1 << _delta_capability_platform | (delta_u32)1 << _delta_capability_profile;
	local.legacy_version = HALO_PORT_NETWORK_VERSION;
	delta_platform_policy_default(_delta_platform_pc_linux, &local.key);
	local.key.platform = _delta_platform_pc_linux;
	local.key.version = DELTA_PLATFORM_KEY_VERSION;
	snprintf(local.build, sizeof(local.build), "host test");
	delta_peer_initialize(&host, &env, &local);

	memset(machines, 0, sizeof(machines));
	machines[0].machine_index = 0;
	machines[0].local = 1;
	machines[1].machine_index = 1;
	machines[1].ipv4 = CLIENT_IP;

	/* 1: the handshake */
	printf("handshake\n");
	for (now = 1000; now < 4000; now += 100)
	{
		delta_peer_client_frame(&client, now, 1, HOST_IP, 4000, 1, 1, players);
		deliver(&host, HOST_IP, CLIENT_IP, now);
		delta_peer_host_frame(&host, now, machines, 2, players);
		deliver(&client, CLIENT_IP, HOST_IP, now);
	}
	CHECK(client.client_state == _delta_peer_client_delta);
	memset(&other, 0, sizeof(other));
	CHECK(delta_peer_machine(&host, 1, &other) && other.known);
	CHECK(other.key.platform == _delta_platform_xbox);
	CHECK(other.key.memory_class == 6);
	CHECK(other.capabilities == ((delta_u32)1 << _delta_capability_platform));
	CHECK(delta_peer_room_limit(&client, _delta_peer_limit_players) <= 16);
	(void)from_client;

	/* 1b: identity, and the legacy table */
	{
		static struct delta_peer identified, desktop;
		struct delta_peer_env client_env = env, host_env = env;

		host_env.legacy_table_serial = host_table_serial;
		host_env.legacy_table_signed = host_table_signed;
		host_env.legacy_table_offer = host_table_offer;
		client_env.legacy_table_serial = console_table_serial;

		printf("identity and no table\n");
		console_local_identified(&local);
		delta_peer_initialize(&identified, &client_env, &local);
		memset(&local, 0, sizeof(local));
		local.capabilities = (delta_u32)1 << _delta_capability_platform | (delta_u32)1 << _delta_capability_profile;
		local.legacy_version = HALO_PORT_NETWORK_VERSION;
		delta_platform_policy_default(_delta_platform_pc_linux, &local.key);
		local.key.platform = _delta_platform_pc_linux;
		local.key.version = DELTA_PLATFORM_KEY_VERSION;
		delta_peer_initialize(&desktop, &host_env, &local);
		tables_sent = 0;
		handshake(&identified, &desktop, 30000);
		CHECK(identified.client_state == _delta_peer_client_delta);
		CHECK(desktop.peers[1].has_profile);
		CHECK(!memcmp(desktop.peers[1].profile.player_id, test_player_id, sizeof(test_player_id)));
		CHECK(tables_sent == 0);

		/* an anonymous console: no ID reaches the host */
		console_local(&local);
		delta_peer_initialize(&identified, &client_env, &local);
		memset(&local, 0, sizeof(local));
		local.capabilities = (delta_u32)1 << _delta_capability_platform | (delta_u32)1 << _delta_capability_profile;
		local.legacy_version = HALO_PORT_NETWORK_VERSION;
		delta_platform_policy_default(_delta_platform_pc_linux, &local.key);
		local.key.platform = _delta_platform_pc_linux;
		local.key.version = DELTA_PLATFORM_KEY_VERSION;
		delta_peer_initialize(&desktop, &host_env, &local);
		tables_sent = 0;
		handshake(&identified, &desktop, 60000);
		CHECK(!desktop.peers[1].has_profile);
		CHECK(tables_sent == 0);

		/* the control: a console that says nothing about tables is sent one */
		console_local(&local);
		client_env.legacy_table_serial = NULL;
		delta_peer_initialize(&identified, &client_env, &local);
		memset(&local, 0, sizeof(local));
		local.capabilities = (delta_u32)1 << _delta_capability_platform;
		local.legacy_version = HALO_PORT_NETWORK_VERSION;
		delta_platform_policy_default(_delta_platform_pc_linux, &local.key);
		local.key.platform = _delta_platform_pc_linux;
		local.key.version = DELTA_PLATFORM_KEY_VERSION;
		delta_peer_initialize(&desktop, &host_env, &local);
		tables_sent = 0;
		handshake(&identified, &desktop, 90000);
		CHECK(tables_sent > 0);
	}
	(void)machines;

	/* 2: no host that speaks Delta: legacy, quietly */
	printf("no answer\n");
	delta_peer_stop(&client);
	queued = 0;
	console_local(&local);
	delta_peer_initialize(&client, &env, &local);
	for (now = 10000; now < 10000 + DELTA_PEER_HANDSHAKE_TIME + 1000; now += 100)
		delta_peer_client_frame(&client, now, 1, HOST_IP, 4000, 1, 1, players);
	CHECK(client.client_state != _delta_peer_client_delta);

	/* 3: noise */
	printf("noise\n");
	{
		unsigned char datagram[DELTA_WIRE_MAXIMUM_DATAGRAM];
		int round;

		srand(7);
		for (round = 0; round < 200000; round++)
		{
			int size = 1 + rand() % (int)sizeof(datagram), at;

			for (at = 0; at < size; at++)
				datagram[at] = (unsigned char)rand();
			/* (a header of the right shape now and then) */
			if (round % 3 == 0 && size > 8)
				{ datagram[0] = DELTA_WIRE_MAGIC_0; datagram[1] = DELTA_WIRE_MAGIC_1; }
			delta_peer_receive(&client, 20000 + (delta_u32)round, HOST_IP, 4000, datagram, size);
		}
	}
	printf("%s\n", failures ? "delta_console_test: FAILED" : "delta_console_test: ok");
	return failures ? 1 : 0;
}
