/*
 * a small http/1.1 client over plat's sockets: GET, the body to a callback.
 * content-length and chunked bodies, one redirect to another http url.
 * https:// urls go through the game's console HTTPS client
 * (chupathingyce/port/console/https), for the hosts it trusts: the store's
 * api (its lists, and the BLAKE2b hashes the map files are checked against).
 * the map files themselves come over plain http (http_plain_url), which
 * keeps them off the one-request lock the console's HTTPS has
 */

#ifndef HTTP_H
#define HTTP_H

/* the body as it comes: 0 from it stops the transfer */
// return 0 from this to cancel the download
typedef int (*http_body_proc)(const char *data, int length, void *context);
/* the size once the headers say it (0 when they don't) */
typedef void (*http_size_proc)(unsigned long size, void *context);

/* the status (200...), or -1 with why in error */
int http_get(const char *url, http_size_proc size, http_body_proc body, void *context, char *error, int error_size);
/* base (http:// or https://host) and path as a plain http:// url, for a
large file checked by a hash got over https */
void http_plain_url(char *out, int size, const char *base, const char *path);

#endif
