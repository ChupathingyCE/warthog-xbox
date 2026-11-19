/*
XBOX_STACK.H

How much of each thread's stack the game uses at most (src/xbox_stack.c):
the main thread's, and the port's own threads' (the tunnel, the game list,
the reports, the claim, the profile link, the Map Browser, the watchdog).
The retail game gave every thread but the main one 16 KB; ours are sized
by guesses from -fstack-usage, and each KB of a stack is committed memory
a 64 MB console is short of. These lines in debug.txt are what a smaller
size has to be checked against.
*/

#ifndef __XBOX_STACK_H
#define __XBOX_STACK_H

/* the main thread's (shell_xbox.c, main.c) */
void xbox_stack_startup(void);
void xbox_stack_update(void);

/* a thread's start: the part of its stack below the caller filled with a
pattern, and the thread counted under name (a string that lives as long
as the game); size is what CreateThread was asked for, and the stack is
measured only when the thread's own bounds agree with it. The slot it
returns (or -1, not measured) goes to xbox_stack_thread_end before the
thread returns, which logs what it used at most. While it runs, the main
loop logs each new high (xbox_stack_update). */
int xbox_stack_thread_begin(const char *name, unsigned long size);
void xbox_stack_thread_end(int slot);

/* a thread function that runs entry between the two */
#define XBOX_STACK_THREAD(wrapper, name, size, entry) \
	static unsigned long __stdcall wrapper(void *parameter) \
	{ \
		int stack_slot = xbox_stack_thread_begin(name, size); \
		unsigned long stack_result = (unsigned long)entry(parameter); \
		xbox_stack_thread_end(stack_slot); \
		return stack_result; \
	}

#endif
