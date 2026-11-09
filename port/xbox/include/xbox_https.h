/*
XBOX_HTTPS.H

HTTPS on the original Xbox (port/xbox/src/xbox_https.c): the game's shared
client (chupathingyce/port/console/https, docs/console-https.md) over XNet.
Requests block, and need CONSOLE_HTTPS_STACK_SIZE of the calling thread's
stack: they run on the game list's thread (xbox_game_list.c), whose stack
is sized for them.
*/

#ifndef __XBOX_HTTPS_H
#define __XBOX_HTTPS_H

/* the client set up (once the network is up; any thread, once) */
void xbox_https_start(void);
/* the signed legacy table's cache read and the game's network rules told of
it (the main thread's, once) */
void xbox_https_table_start(void);
/* the signed legacy table fetched again, when it is due (at the first call,
then every 4 hours, or 30 minutes after a failure); blocking, on a thread
with CONSOLE_HTTPS_STACK_SIZE to spare */
void xbox_https_table_refresh(void);

#endif
