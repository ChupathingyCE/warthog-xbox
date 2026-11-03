/*
 * a small http/1.1 client over plat's sockets: GET, the body to a callback.
 * plain http only (the xbox has no tls here): the store's api answers on
 * http://api.xboxhomebrew.store. content-length and chunked bodies, one
 * redirect to another http url
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

#endif
