/*
PTHREAD.H (the console's)

What internet play's sources use of POSIX threads (port/xbox/p2p/
platform.h), over the kernel's: a mutex made on first use (so a static
PTHREAD_MUTEX_INITIALIZER works), a function run once (the legacy table's
load, delta.c), and threads that run detached.
port/xbox/src/xbox_p2p.c.
*/

#ifndef __HALO_XBOX_P2P_PTHREAD_H
#define __HALO_XBOX_P2P_PTHREAD_H

typedef struct
{
	/* 0: not made yet; 1: being made; 2: made */
	volatile long state;
	/* (a CRITICAL_SECTION's room, kept opaque here: xbox_p2p.c checks it) */
	void *storage[8];
} pthread_mutex_t;

typedef unsigned long pthread_t;
typedef int pthread_attr_t;
typedef int pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }

/* 0: not run yet; 1: running; 2: done */
typedef volatile long pthread_once_t;
#define PTHREAD_ONCE_INIT 0

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_once(pthread_once_t *once, void (*function)(void));
int pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*start)(void *), void *argument);
int pthread_detach(pthread_t thread);

#endif
