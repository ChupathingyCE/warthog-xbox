/*
XBOX_STACK.C

How much of the main thread's stack the game has used, at most. The stack
is the XBE's (/stack in port/xbox/build.py: 768 KB; the January build's
was 1 MB, the retail game's 512 KB), and the console gives a thread all of
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
*/

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
	_stack_check_interval = 300
};

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

void xbox_stack_update(void)
{
	unsigned long *word;
	unsigned long used;

	if (!stack.limit || ++stack.update_count < _stack_check_interval)
		return;
	stack.update_count = 0;
	for (word = stack.limit; word < stack.base && *word == _stack_pattern; word++)
		;
	used = (unsigned long)((stack.base - word) * sizeof(*word));
	if (used >= stack.used_logged + _stack_log_step)
	{
		stack.used_logged = used;
		platform_log("stack: %lu KB of the main thread's %lu KB used at most (a new high)", used >> 10,
			(unsigned long)((stack.base - stack.limit) * sizeof(*stack.base)) >> 10);
	}
}
