/*
XBOX_STACK.C

How much of the main thread's stack the game has used, at most. The stack
is the XBE's (/stack in port/xbox/build.py: 512 KB, the retail game's; the
January build's was 1 MB, then 768 KB), and the console gives a thread all of
its stack up front, so each KB of it is a KB the heap does not have. Some
of the debug decomp's functions keep large arrays on the stack (the AI's
firing positions and flee searches, near 100 KB each); the deepest seen in
any debug.txt is 279 KB, a campaign mission.

At the start, the part of the stack below the caller is filled with a
pattern (xbox_stack_startup); every few seconds the main loop looks for
the lowest word no longer holding it (xbox_stack_update), and debug.txt
gets "stack: N KB of the main thread's M KB used at most (a new high)" each
time that grows by 16 KB. Interrupts on the console run on the stack of the
thread they come on, below its stack pointer; they may write over the
pattern, which only counts them in.

The port's own threads do the same with their stacks
(xbox_stack_thread_begin, include/xbox_stack.h): debug.txt gets
"stack: thread NAME used N KB of its M KB at most" when one ends, and a
line at each new high (4 KB steps) while one runs, read by the main loop.
*/

#include "../include/xbox_stack.h"

void __stdcall Sleep(unsigned long milliseconds);

/* the game's log (port/xbox/src/xbox_port.c) and the start's (xbox_memory.c) */
void platform_log(const char *format, ...);
void xbox_boot_log(const char *format, ...);

enum
{
	_stack_pattern = 0x5AC4ED00,
	/* what is left unfilled below the caller (xbox_stack_startup's own
	frame and the calls it makes) */
	_stack_margin = 0x1000,
	_stack_log_step = 0x4000,
	/* the bounds are believed only for a stack of this size or less */
	_stack_largest = 0x400000,
	/* how often xbox_stack_update looks (main loop passes) */
	_stack_check_interval = 300,
	_thread_log_step = 0x1000,
	_thread_slots = 8
};

enum
{
	_slot_free,
	_slot_running,
	/* (the main loop is reading it: its thread waits to end) */
	_slot_reading
};

static struct
{
	long state;
	const char *name;
	unsigned long *limit;
	unsigned long *base;
	unsigned long used_logged;
} threads[_thread_slots];

static struct
{
	unsigned long *limit;
	unsigned long *base;
	unsigned long used_logged;
	long update_count;
} stack;

/* the thread's stack, from its thread information block (the processor's
FS segment: the stack's top at 4, its bottom at 8, as on Windows) */
static void stack_bounds(unsigned long **limit, unsigned long **base)
{
	unsigned long top, bottom;

	__asm__ volatile("movl %%fs:4, %0" : "=r"(top));
	__asm__ volatile("movl %%fs:8, %0" : "=r"(bottom));
	*base = (unsigned long *)top;
	*limit = (unsigned long *)bottom;
}

void xbox_stack_startup(void)
{
	unsigned long *limit, *base, *end, *word;
	unsigned long here = (unsigned long)&limit;

	stack_bounds(&limit, &base);
	if (!limit || base <= limit || (unsigned long)(base - limit) * sizeof(*base) > _stack_largest ||
		here <= (unsigned long)limit + _stack_margin || here >= (unsigned long)base)
	{
		xbox_boot_log("stack: the main thread's bounds are not known (%p to %p), not measured", (void *)limit,
			(void *)base);
		return;
	}
	end = (unsigned long *)((here - _stack_margin) & ~3UL);
	for (word = limit; word < end; word++)
		*word = _stack_pattern;
	stack.limit = limit;
	stack.base = base;
	stack.used_logged = (unsigned long)((base - end) * sizeof(*base));
	xbox_boot_log("stack: the main thread's %lu KB, %lu KB used at the start",
		(unsigned long)((base - limit) * sizeof(*base)) >> 10, stack.used_logged >> 10);
}

static unsigned long stack_used(unsigned long *limit, unsigned long *base);
static void stack_threads_update(void);

void xbox_stack_update(void)
{
	unsigned long used;

	if (++stack.update_count < _stack_check_interval)
		return;
	stack.update_count = 0;
	stack_threads_update();
	if (!stack.limit)
		return;
	used = stack_used(stack.limit, stack.base);
	if (used >= stack.used_logged + _stack_log_step)
	{
		stack.used_logged = used;
		platform_log("stack: %lu KB of the main thread's %lu KB used at most (a new high)", used >> 10,
			(unsigned long)((stack.base - stack.limit) * sizeof(*stack.base)) >> 10);
	}
}

/* the lowest word of a stack no longer holding the pattern: what it used */
static unsigned long stack_used(unsigned long *limit, unsigned long *base)
{
	unsigned long *word;

	for (word = limit; word < base && *word == _stack_pattern; word++)
		;
	return (unsigned long)((base - word) * sizeof(*word));
}

int xbox_stack_thread_begin(const char *name, unsigned long size)
{
	unsigned long *limit, *base, *end, *word;
	unsigned long here = (unsigned long)&limit;
	unsigned long bytes;
	int slot;

	stack_bounds(&limit, &base);
	bytes = (unsigned long)((base - limit) * sizeof(*base));
	/* (only a stack the thread is on, and of about the size asked for:
	the pattern goes nowhere else) */
	if (!limit || base <= limit || bytes < size || bytes > size + 0x10000 || here <= (unsigned long)limit + _stack_margin ||
		here >= (unsigned long)base)
	{
		platform_log("stack: thread %s's bounds are not known (%p to %p, asked for %lu KB), not measured", name,
			(void *)limit, (void *)base, size >> 10);
		return -1;
	}
	for (slot = 0; slot < _thread_slots; slot++)
	{
		long expected = _slot_free;

		if (__atomic_compare_exchange_n(&threads[slot].state, &expected, _slot_reading, 0, __ATOMIC_SEQ_CST,
			__ATOMIC_SEQ_CST))
			break;
	}
	if (slot == _thread_slots)
		return -1;
	end = (unsigned long *)((here - _stack_margin) & ~3UL);
	for (word = limit; word < end; word++)
		*word = _stack_pattern;
	threads[slot].name = name;
	threads[slot].limit = limit;
	threads[slot].base = base;
	threads[slot].used_logged = (unsigned long)((base - end) * sizeof(*base));
	__atomic_store_n(&threads[slot].state, _slot_running, __ATOMIC_SEQ_CST);
	return slot;
}

void xbox_stack_thread_end(int slot)
{
	unsigned long used;

	if (slot < 0 || slot >= _thread_slots)
		return;
	for (;;)
	{
		long expected = _slot_running;

		if (__atomic_compare_exchange_n(&threads[slot].state, &expected, _slot_reading, 0, __ATOMIC_SEQ_CST,
			__ATOMIC_SEQ_CST))
			break;
		Sleep(0);
	}
	used = stack_used(threads[slot].limit, threads[slot].base);
	platform_log("stack: thread %s used %lu KB of its %lu KB at most", threads[slot].name, used >> 10,
		(unsigned long)((threads[slot].base - threads[slot].limit) * sizeof(*threads[slot].base)) >> 10);
	__atomic_store_n(&threads[slot].state, _slot_free, __ATOMIC_SEQ_CST);
}

/* (the main loop's look at the threads still running, from xbox_stack_update) */
static void stack_threads_update(void)
{
	int slot;

	for (slot = 0; slot < _thread_slots; slot++)
	{
		long expected = _slot_running;
		unsigned long used;

		if (!__atomic_compare_exchange_n(&threads[slot].state, &expected, _slot_reading, 0, __ATOMIC_SEQ_CST,
			__ATOMIC_SEQ_CST))
			continue;
		used = stack_used(threads[slot].limit, threads[slot].base);
		if (used >= threads[slot].used_logged + _thread_log_step)
		{
			threads[slot].used_logged = used;
			platform_log("stack: thread %s used %lu KB of its %lu KB at most (a new high)", threads[slot].name,
				used >> 10, (unsigned long)((threads[slot].base - threads[slot].limit) * sizeof(*threads[slot].base)) >> 10);
		}
		__atomic_store_n(&threads[slot].state, _slot_running, __ATOMIC_SEQ_CST);
	}
}
