/*
DELTA_JOIN_TEST.C

Which hosts the console joins (warthog-xbox #59): the game's join check
(network_game_client_advertised_game_compatible) goes by a Delta host's
game wire and range on the console, as on the desktop builds, through
delta_compat.c as the console builds it. What it checks, for the console's
own wire (DELTA_WIRE) and number window (HALO_PORT_NETWORK_VERSION_MINIMUM
to _MAXIMUM):

- a Delta host of the old layout (0.8.0h's: the flag, the old slot, the
  range bytes) is joined whatever OpenCE number it announces, past the
  window too;
- so is a host of the Delta tag at the reserved bytes' tail (no flag);
- a Delta host of a wire that broke with the console's is refused, named;
- a host without Delta (OpenCE's) leaves it to the numbers.
*/

#include "delta_compat.h"
#include "delta.h"
#include "halo_port_limits.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

enum { RESERVED = 0x1A };

static void announce(unsigned char *reserved, int version)
{
	memset(reserved, 0, RESERVED);
	reserved[0] = (unsigned char)(version & 0xFF);
	reserved[1] = (unsigned char)(version >> 8);
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] = HALO_PORT_ADVERTISED_DISTRIBUTED_FLAG;
}

/* a 0.8.0h host: the flag, then its oldest wire in the old slot and its range */
static void old_host(unsigned char *reserved, int version, const char *wire)
{
	announce(reserved, version);
	delta_compat_advertise(reserved, RESERVED, DELTA_MAJOR, wire);
	reserved[DELTA_ADVERTISED_FLAGS_OFFSET] |= DELTA_ADVERTISED_FLAG;
}

/* a host of the tag, by hand from its layout (docs/delta.md) */
static void tag_host(unsigned char *reserved, int version, int number, int oldest)
{
	unsigned char *tag = reserved + DELTA_ADVERTISED_TAG_OFFSET;
	unsigned char check = 0xA5;
	int index;

	announce(reserved, version);
	tag[0] = 'D';
	tag[1] = 'L';
	tag[2] = DELTA_MAJOR;
	tag[3] = 1;
	tag[4] = (unsigned char)number;
	tag[5] = 'a';
	tag[6] = (unsigned char)oldest;
	tag[7] = 'a';
	for (index = 0; index < 8; index++)
		check = (unsigned char)(check ^ tag[index]);
	tag[8] = check;
}

/* the console's decision, as delta_peer_advertised_wire_matches makes it */
static int console_decides(const unsigned char *reserved, char *theirs, int size)
{
	if (!delta_compat_speaks_delta(reserved, RESERVED))
		return -1;
	return delta_compat_decide(reserved, RESERVED, DELTA_MAJOR, DELTA_WIRE, theirs, size);
}

int main(void)
{
	unsigned char reserved[RESERVED];
	char theirs[24];
	int past = HALO_PORT_NETWORK_VERSION_MAXIMUM + 1;
	int versions[4];
	int index;

	versions[0] = HALO_PORT_NETWORK_VERSION;
	versions[1] = HALO_PORT_NETWORK_VERSION_MAXIMUM;
	versions[2] = past;
	versions[3] = HALO_PORT_NETWORK_VERSION_MINIMUM - 1;
	for (index = 0; index < 4; index++)
	{
		old_host(reserved, versions[index], "chupa-38a");
		CHECK(console_decides(reserved, theirs, (int)sizeof(theirs)) == 1);
		old_host(reserved, versions[index], "chupa-32a");
		CHECK(console_decides(reserved, theirs, (int)sizeof(theirs)) == 1);
		tag_host(reserved, versions[index], 39, 28);
		CHECK(console_decides(reserved, theirs, (int)sizeof(theirs)) == 1);
	}
	/* a wire that broke with the console's: refused, by its name */
	tag_host(reserved, HALO_PORT_NETWORK_VERSION, 60, 50);
	CHECK(console_decides(reserved, theirs, (int)sizeof(theirs)) == 0 && !strcmp(theirs, "chupa-60a"));
	old_host(reserved, HALO_PORT_NETWORK_VERSION, "chupa-60a");
	CHECK(console_decides(reserved, theirs, (int)sizeof(theirs)) == 0 && !strcmp(theirs, "chupa-60a"));
	/* OpenCE's hosts: the numbers decide */
	announce(reserved, past);
	CHECK(console_decides(reserved, theirs, (int)sizeof(theirs)) == -1);
	announce(reserved, HALO_PORT_NETWORK_VERSION);
	CHECK(console_decides(reserved, theirs, (int)sizeof(theirs)) == -1);
	if (failures)
	{
		fprintf(stderr, "delta_join_test: %d failures\n", failures);
		return 1;
	}
	printf("delta_join_test: ok (%s, numbers %d to %d)\n", DELTA_WIRE, HALO_PORT_NETWORK_VERSION_MINIMUM,
		HALO_PORT_NETWORK_VERSION_MAXIMUM);
	return 0;
}
