/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 JizzyRivers and Warthog's contributors
 * Part of Warthog: under its license, with its console SDK permission (README.md, "License").
 */

/*
 * the platform under the browser: files, folders, the network. plat_xbox.c on
 * the console (nxdk), plat_posix.c on linux, for testing the same code against
 * the real server
 */

#ifndef PLAT_H
#define PLAT_H

/* the path separator: '\\' on the xbox, '/' on linux */
// nxdk defines NXDK for us, linux builds get /
#ifdef NXDK
#define PLAT_SEP '\\'
#else
#define PLAT_SEP '/'
#endif

int plat_init(char *status, int size);
int plat_is_file(const char *path);
int plat_is_dir(const char *path);
int plat_mkdir(const char *path);
int plat_remove(const char *path);
int plat_rmdir(const char *path);

/* each entry of a folder (not . or ..): 0 from found stops it */
typedef int (*plat_found_proc)(const char *name, int is_dir, void *context);
void plat_list(const char *path, plat_found_proc found, void *context);

/* the folders the browser looks in for warthog (drive roots on the xbox) */
const char *const *plat_roots(void);

/* where maps go when warthog isn't found */
const char *plat_fallback_dir(void);

/* sockets: lwip's on the xbox, the system's on linux */
// sockets. returns the socket or -1 w the error
int plat_connect(const char *host, int port, char *error, int error_size);
int plat_send(int socket, const char *data, int length);
int plat_recv(int socket, char *buffer, int size);
void plat_close(int socket);

#endif
