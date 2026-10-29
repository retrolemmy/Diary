/*
    Diary Server client: HTTP/1.0 over the MacTCP driver (.IPP).

    Each request opens its own TCP stream: create, connect, send, read the
    reply, then abort and release it. The driver is only opened when a
    server is used, so Diary runs on Macs without MacTCP too.
*/

#include <Devices.h>
#include <Memory.h>
#include <stddef.h>
#include <string.h>

#include "net.h"

/* The MacTCP layout must match Apple's (checked against MacTCP.h 2.0.6). */
#if defined(__m68k__)
_Static_assert(offsetof(TCPiopb, csCode) == 26, "TCPiopb.csCode");
_Static_assert(offsetof(TCPiopb, csParam) == 32, "TCPiopb.csParam");
_Static_assert(offsetof(TCPOpenPB, remotePort) == 8, "TCPOpenPB.remotePort");
_Static_assert(offsetof(TCPOpenPB, userDataPtr) == 62, "TCPOpenPB.userDataPtr");
_Static_assert(offsetof(TCPSendPB, wdsPtr) == 6, "TCPSendPB.wdsPtr");
_Static_assert(offsetof(TCPReceivePB, rcvBuff) == 4, "TCPReceivePB.rcvBuff");
_Static_assert(offsetof(TCPReceivePB, rcvBuffLen) == 8, "TCPReceivePB.rcvBuffLen");
_Static_assert(offsetof(TCPReceivePB, rdsPtr) == 10, "TCPReceivePB.rdsPtr");
_Static_assert(sizeof(wdsEntry) == 6, "wdsEntry");
#endif

enum {
    kDefaultPort = 8642,
    kConnectSeconds = 10,       /* waiting for the server to accept */
    kReplySeconds = 15,         /* waiting for each part of the reply */
    kStreamBufferSize = 16384,  /* MacTCP's own receive buffer */
    kMaxReply = 80 * 1024
};

static short gRefNum;
static Boolean gDriverOpen;
static ip_addr gHost;
static Boolean gHostValid;
static tcp_port gPort = kDefaultPort;
static char gHostText[64];
static char gKey[64];

static void SetError(char *error, long size, const char *text, const char *detail)
{
    strncpy(error, text, size - 1);
    error[size - 1] = 0;
    if (detail != NULL && strlen(error) + strlen(detail) < (size_t)size - 1)
        strcat(error, detail);
}

/* Parses a dotted IP address such as 192.168.1.10; false for anything else. */
static Boolean ParseAddress(const char *text, long length, ip_addr *address)
{
    unsigned long value = 0, part = 0;
    int dots = 0, digits = 0;
    long i;

    for (i = 0; i < length; i++) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            part = part * 10 + (c - '0');
            if (part > 255 || ++digits > 3)
                return false;
        } else if (c == '.' && digits > 0 && dots < 3) {
            value = (value << 8) | part;
            part = 0;
            digits = 0;
            dots++;
        } else {
            return false;
        }
    }
    if (dots != 3 || digits == 0)
        return false;
    *address = (value << 8) | part;
    return true;
}

void NetSetServer(const char *address, const char *key)
{
    const char *colon = strchr(address, ':');
    long length = colon != NULL ? colon - address : (long)strlen(address);

    if (length >= (long)sizeof(gHostText))
        length = sizeof(gHostText) - 1;
    memcpy(gHostText, address, length);
    gHostText[length] = 0;
    gHostValid = ParseAddress(address, length, &gHost);

    gPort = kDefaultPort;
    if (colon != NULL) {
        long port = 0;
        const char *p;
        for (p = colon + 1; *p >= '0' && *p <= '9'; p++)
            port = port * 10 + (*p - '0');
        if (port > 0 && port < 65536)
            gPort = port;
    }
    strncpy(gKey, key, sizeof(gKey) - 1);
    gKey[sizeof(gKey) - 1] = 0;
}

void NetEncode(const char *text, char *out, long outSize)
{
    static const char kHex[] = "0123456789ABCDEF";
    long n = 0;

    for (; *text != 0 && n + 4 < outSize; text++) {
        unsigned char c = *text;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || c == '-' || c == '.' || c == '_') {
            out[n++] = c;
        } else {
            out[n++] = '%';
            out[n++] = kHex[c >> 4];
            out[n++] = kHex[c & 15];
        }
    }
    out[n] = 0;
}

static void AppendNumber(char *s, long n)
{
    char digits[12];
    int count = 0;

    s += strlen(s);
    do {
        digits[count++] = '0' + n % 10;
        n /= 10;
    } while (n > 0);
    while (count > 0)
        *s++ = digits[--count];
    *s = 0;
}

static OSErr CallDriver(TCPiopb *pb, short csCode, StreamPtr stream)
{
    pb->ioCompletion = NULL;
    pb->ioCRefNum = gRefNum;
    pb->csCode = csCode;
    pb->tcpStream = stream;
    return PBControlSync((ParmBlkPtr)pb);
}

/* Case-insensitive test whether line starts with name (e.g. "Content-Length:"). */
static Boolean HeaderIs(const char *line, const char *name)
{
    for (; *name != 0; line++, name++) {
        char a = *line, b = *name;
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (b >= 'A' && b <= 'Z')
            b += 32;
        if (a != b)
            return false;
    }
    return true;
}

/* Offset of the blank line ending the headers, or -1 if it hasn't arrived. */
static long HeaderEnd(const char *buf, long length)
{
    long i;

    for (i = 0; i + 3 < length; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i;
    return -1;
}

/* True once the whole reply (headers plus Content-Length bytes) is in. */
static Boolean ReplyComplete(char *buf, long length)
{
    long end = HeaderEnd(buf, length), contentLength = -1;
    char saved;
    const char *line;

    if (end < 0)
        return false;
    saved = buf[end];
    buf[end] = 0;
    for (line = strchr(buf, '\n'); line != NULL; line = strchr(line, '\n')) {
        line++;
        if (HeaderIs(line, "Content-Length:")) {
            const char *p = line + 15;
            while (*p == ' ')
                p++;
            for (contentLength = 0; *p >= '0' && *p <= '9'; p++)
                contentLength = contentLength * 10 + (*p - '0');
        }
    }
    buf[end] = saved;
    return contentLength >= 0 && length >= end + 4 + contentLength;
}

/* Splits a raw reply into status, version and body (moved to the buffer's start). */
static Boolean ParseReply(char *buf, long length, HttpResponse *response)
{
    long end = HeaderEnd(buf, length);
    char *line;

    if (end < 0 || strncmp(buf, "HTTP/", 5) != 0)
        return false;
    buf[end] = 0;

    line = strchr(buf, ' ');
    if (line == NULL)
        return false;
    response->status = 0;
    for (line++; *line >= '0' && *line <= '9'; line++)
        response->status = response->status * 10 + (*line - '0');

    response->version[0] = 0;
    for (line = strchr(buf, '\n'); line != NULL; line = strchr(line, '\n')) {
        line++;
        if (HeaderIs(line, "X-Diary-Version:")) {
            const char *v = line + 16;
            int n = 0;
            while (*v == ' ')
                v++;
            while (*v > ' ' && n < (int)sizeof(response->version) - 1)
                response->version[n++] = *v++;
            response->version[n] = 0;
        }
    }

    response->length = length - (end + 4);
    BlockMove(buf + end + 4, buf, response->length);
    buf[response->length] = 0;
    response->body = buf;
    return true;
}

static Boolean OpenTCP(char *error, long errorSize)
{
    unsigned char name[5] = { 4, '.', 'I', 'P', 'P' };

    if (!gDriverOpen)
        gDriverOpen = OpenDriver(name, &gRefNum) == noErr;
    if (!gDriverOpen)
        SetError(error, errorSize, "MacTCP or Open Transport isn't available.", NULL);
    return gDriverOpen;
}

Boolean HttpRequest(const char *method, const char *path,
                    const char *body, long bodyLength, const char *base,
                    HttpResponse *response, char *error, long errorSize)
{
    char header[512];
    TCPiopb pb;
    wdsEntry wds[3];
    Ptr streamBuffer = NULL;
    char *buf = NULL;
    StreamPtr stream = NULL;
    long length = 0;
    Boolean ok = false;
    OSErr err;

    response->status = 0;
    response->version[0] = 0;
    response->body = NULL;
    response->length = 0;

    if (!gHostValid) {
        SetError(error, errorSize, "Enter the server's IP address, like 192.168.1.10 "
                 "(Diary for the Mac can't look up server names).", NULL);
        return false;
    }
    if (strlen(path) + strlen(gHostText) + strlen(gKey) + 200 > sizeof(header)) {
        SetError(error, errorSize, "The request is too long.", NULL);
        return false;
    }
    if (!OpenTCP(error, errorSize))
        return false;

    strcpy(header, method);
    strcat(header, " ");
    strcat(header, path);
    strcat(header, strchr(path, '?') != NULL ? "&charset=macroman" : "?charset=macroman");
    strcat(header, " HTTP/1.0\r\nHost: ");
    strcat(header, gHostText);
    strcat(header, "\r\nUser-Agent: Diary for the Mac\r\n");
    if (gKey[0] != 0) {
        strcat(header, "X-Diary-Key: ");
        strcat(header, gKey);
        strcat(header, "\r\n");
    }
    if (base != NULL) {
        strcat(header, "X-Diary-Base: ");
        strcat(header, base);
        strcat(header, "\r\n");
    }
    if (body != NULL) {
        strcat(header, "Content-Length: ");
        AppendNumber(header, bodyLength);
        strcat(header, "\r\n");
    }
    strcat(header, "\r\n");

    streamBuffer = NewPtr(kStreamBufferSize);
    buf = NewPtr(kMaxReply + 1);
    if (streamBuffer == NULL || buf == NULL) {
        SetError(error, errorSize, "Not enough memory.", NULL);
        goto done;
    }

    memset(&pb, 0, sizeof(pb));
    pb.csParam.create.rcvBuff = streamBuffer;
    pb.csParam.create.rcvBuffLen = kStreamBufferSize;
    if (CallDriver(&pb, TCPCreate, NULL) != noErr) {
        SetError(error, errorSize, "Can't create a network connection.", NULL);
        goto done;
    }
    stream = pb.tcpStream;

    memset(&pb.csParam, 0, sizeof(pb.csParam));
    pb.csParam.open.ulpTimeoutValue = kConnectSeconds;
    pb.csParam.open.ulpTimeoutAction = 1;       /* abort */
    pb.csParam.open.validityFlags = timeoutValue | timeoutAction;
    pb.csParam.open.remoteHost = gHost;
    pb.csParam.open.remotePort = gPort;
    err = CallDriver(&pb, TCPActiveOpen, stream);
    if (err != noErr) {
        if (err == openFailed || err == connectionTerminated) {
            SetError(error, errorSize, "The server refused the connection. Is Diary Server running on ", gHostText);
            if (strlen(error) + 1 < (size_t)errorSize)
                strcat(error, "?");
        } else if (err == commandTimeout)
            SetError(error, errorSize, "The server didn't answer: ", gHostText);
        else
            SetError(error, errorSize, "Can't connect to the server ", gHostText);
        goto done;
    }

    wds[0].length = strlen(header);
    wds[0].ptr = header;
    wds[1].length = body != NULL ? bodyLength : 0;
    wds[1].ptr = (Ptr)body;
    wds[2].length = 0;
    wds[2].ptr = NULL;
    if (wds[1].length == 0) {
        wds[1].ptr = NULL;          /* the list ends at the first empty entry */
    }
    memset(&pb.csParam, 0, sizeof(pb.csParam));
    pb.csParam.send.ulpTimeoutValue = kReplySeconds;
    pb.csParam.send.ulpTimeoutAction = 1;
    pb.csParam.send.validityFlags = timeoutValue | timeoutAction;
    pb.csParam.send.pushFlag = true;
    pb.csParam.send.wdsPtr = (Ptr)wds;
    if (CallDriver(&pb, TCPSend, stream) != noErr) {
        SetError(error, errorSize, "The connection to the server broke off.", NULL);
        goto done;
    }

    /* Read until the whole reply is in or the server closes the connection. */
    while (length < kMaxReply && !ReplyComplete(buf, length)) {
        long room = kMaxReply - length;

        memset(&pb.csParam, 0, sizeof(pb.csParam));
        pb.csParam.receive.commandTimeoutValue = kReplySeconds;
        pb.csParam.receive.rcvBuff = buf + length;
        pb.csParam.receive.rcvBuffLen = room > 32767 ? 32767 : room;
        err = CallDriver(&pb, TCPRcv, stream);
        if (err != noErr)
            break;
        length += pb.csParam.receive.rcvBuffLen;
    }
    if (!ReplyComplete(buf, length) && !(err == connectionClosing && HeaderEnd(buf, length) >= 0)) {
        SetError(error, errorSize, err == commandTimeout ? "The server stopped answering."
                                                           : "The connection to the server broke off.", NULL);
        goto done;
    }
    if (!ParseReply(buf, length, response)) {
        SetError(error, errorSize, "The server's reply wasn't understood. Is it a Diary Server?", NULL);
        goto done;
    }
    buf = NULL;                     /* now owned by the response */
    ok = true;

done:
    if (stream != NULL) {
        /* The reply is complete (or something failed), so there's nothing
           left to wait for: abort rather than close, then free the stream. */
        CallDriver(&pb, TCPAbort, stream);
        memset(&pb.csParam, 0, sizeof(pb.csParam));
        CallDriver(&pb, TCPRelease, stream);
    }
    if (streamBuffer != NULL)
        DisposePtr(streamBuffer);
    if (buf != NULL)
        DisposePtr(buf);
    return ok;
}

void HttpFree(HttpResponse *response)
{
    if (response->body != NULL)
        DisposePtr(response->body);
    response->body = NULL;
}
