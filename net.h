/*
    Diary Server client: HTTP/1.0 over MacTCP (or Open Transport's MacTCP
    compatibility). See PROTOCOL.md in the Diary-Server repository.
*/
#ifndef DIARY_NET_H
#define DIARY_NET_H

#include <MacTypes.h>

typedef struct {
    long status;            /* HTTP status, e.g. 200 */
    char version[16];       /* the X-Diary-Version header, "" if none */
    char *body;             /* NUL-terminated; free with HttpFree() */
    long length;
} HttpResponse;

/* Sets the server as "a.b.c.d" or "a.b.c.d:port" (MacTCP can't look up
   names here), and the key ("" for none). */
void NetSetServer(const char *address, const char *key);

/* Sends one request and waits for the reply. path is e.g. "/entries" or
   "/search?q=word"; "charset=macroman" is added. base, if not NULL, is sent
   as X-Diary-Base. Returns false with a message in error if the server
   couldn't be reached; HTTP errors such as 404 come back in status. */
Boolean HttpRequest(const char *method, const char *path,
                    const char *body, long bodyLength, const char *base,
                    HttpResponse *response, char *error, long errorSize);

void HttpFree(HttpResponse *response);

/* Percent-encodes text for a query parameter. */
void NetEncode(const char *text, char *out, long outSize);

#endif
