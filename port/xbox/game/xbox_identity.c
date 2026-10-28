/*
XBOX_IDENTITY.C

Which build this is, as the desktop builds' build_identity.c answers it.
The network versions it plays with are the game's delta.c's: the built-in
ones of halo_port_limits.h, widened by the signed legacy table the console
fetches over HTTPS (xbox_https.c) and caches in D:\delta_legacy.signed.
*/

#ifdef HALO_XBOX_CONSOLE

#include "cseries/cseries.h"

#include "halo_port_limits.h"

#include <stdio.h>

/* ---------- build_identity.h */

/* "Warthog (original Xbox, network version 28, built Oct 13 2026)" */
char const *build_identity(
	void)
{
	static char identity[96];

	if (!identity[0])
	{
		snprintf(identity, sizeof(identity), "Warthog (original Xbox, network version %d, built %s)",
			(int)HALO_PORT_NETWORK_VERSION, __DATE__);
	}

	return identity;
}

#endif
