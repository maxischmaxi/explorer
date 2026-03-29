#define _POSIX_C_SOURCE 200809L
#include "debug_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define MAX_CLIENTS    4
#define RECV_BUF_SIZE  4096
#define MAX_MSG_SIZE   8192

/* ---- SHA-1 fuer WebSocket-Handshake (minimale Implementierung) ---- */

static void sha1(const unsigned char *data, size_t len, unsigned char out[20])
{
    uint32_t h0=0x67452301, h1=0xEFCDAB89, h2=0x98BADCFE, h3=0x10325476, h4=0xC3D2E1F0;
    size_t msg_len = len + 1 + 8;
    size_t padded = ((msg_len + 63) / 64) * 64;
    unsigned char *msg = calloc(1, padded);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        msg[padded - 1 - i] = (unsigned char)(bits >> (i * 8));

    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)msg[off+i*4]<<24)|((uint32_t)msg[off+i*4+1]<<16)|
                   ((uint32_t)msg[off+i*4+2]<<8)|msg[off+i*4+3];
        for (int i = 16; i < 80; i++) {
            uint32_t t = w[i-3]^w[i-8]^w[i-14]^w[i-16];
            w[i] = (t<<1)|(t>>31);
        }
        uint32_t a=h0, b=h1, c=h2, d=h3, e=h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i<20)      { f=(b&c)|((~b)&d); k=0x5A827999; }
            else if (i<40) { f=b^c^d;           k=0x6ED9EBA1; }
            else if (i<60) { f=(b&c)|(b&d)|(c&d); k=0x8F1BBCDC; }
            else           { f=b^c^d;           k=0xCA62C1D6; }
            uint32_t tmp = ((a<<5)|(a>>27)) + f + e + k + w[i];
            e=d; d=c; c=(b<<30)|(b>>2); b=a; a=tmp;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e;
    }
    free(msg);
    uint32_t h[5] = {h0,h1,h2,h3,h4};
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (unsigned char)(h[i]>>24);
        out[i*4+1] = (unsigned char)(h[i]>>16);
        out[i*4+2] = (unsigned char)(h[i]>>8);
        out[i*4+3] = (unsigned char)(h[i]);
    }
}

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char *in, size_t len, char *out)
{
    size_t i, j = 0;
    for (i = 0; i + 2 < len; i += 3) {
        out[j++] = b64[in[i]>>2];
        out[j++] = b64[((in[i]&3)<<4)|(in[i+1]>>4)];
        out[j++] = b64[((in[i+1]&0xf)<<2)|(in[i+2]>>6)];
        out[j++] = b64[in[i+2]&0x3f];
    }
    if (i < len) {
        out[j++] = b64[in[i]>>2];
        if (i+1 < len) {
            out[j++] = b64[((in[i]&3)<<4)|(in[i+1]>>4)];
            out[j++] = b64[(in[i+1]&0xf)<<2];
        } else {
            out[j++] = b64[(in[i]&3)<<4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
}

/* ---- WebSocket-Framing ---- */

static int ws_send_text(int fd, const char *msg, size_t len)
{
    unsigned char header[10];
    int hlen = 0;

    header[0] = 0x81; /* FIN + text opcode */
    if (len < 126) {
        header[1] = (unsigned char)len;
        hlen = 2;
    } else if (len < 65536) {
        header[1] = 126;
        header[2] = (unsigned char)(len >> 8);
        header[3] = (unsigned char)(len);
        hlen = 4;
    } else {
        header[1] = 127;
        for (int i = 0; i < 8; i++)
            header[2+i] = (unsigned char)(len >> ((7-i)*8));
        hlen = 10;
    }

    if (write(fd, header, (size_t)hlen) < 0) return -1;
    if (write(fd, msg, len) < 0) return -1;
    return 0;
}

static int ws_decode_frame(const unsigned char *buf, size_t buflen,
                           char *out, size_t out_max, size_t *frame_len)
{
    if (buflen < 2) return -1;

    size_t payload_len = buf[1] & 0x7F;
    int masked = (buf[1] & 0x80) != 0;
    size_t offset = 2;

    if (payload_len == 126) {
        if (buflen < 4) return -1;
        payload_len = ((size_t)buf[2] << 8) | buf[3];
        offset = 4;
    } else if (payload_len == 127) {
        if (buflen < 10) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | buf[2+i];
        offset = 10;
    }

    unsigned char mask[4] = {0};
    if (masked) {
        if (buflen < offset + 4) return -1;
        memcpy(mask, buf + offset, 4);
        offset += 4;
    }

    if (buflen < offset + payload_len) return -1;
    *frame_len = offset + payload_len;

    size_t copy = payload_len < out_max - 1 ? payload_len : out_max - 1;
    for (size_t i = 0; i < copy; i++)
        out[i] = (char)(buf[offset + i] ^ mask[i % 4]);
    out[copy] = '\0';

    return (buf[0] & 0x0F); /* opcode */
}

/* ---- Client State ---- */

typedef struct {
    int  fd;
    int  upgraded; /* 1 = WebSocket handshake done */
    char recv_buf[RECV_BUF_SIZE];
    size_t recv_len;
} DebugClient;

static int server_fd = -1;
static DebugClient clients[MAX_CLIENTS];
static int debug_port;

/* Pending navigate request (set by "navigate" command, consumed by main loop) */
static char *pending_navigate_url = NULL;
static int   pending_navigate_client_fd = -1;

/* ---- Screenshot Command ---- */

static void cmd_screenshot(int client_fd, const char *path)
{
    GLFWwindow *win = glfwGetCurrentContext();
    if (!win) {
        ws_send_text(client_fd, "{\"error\":\"no GL context\"}", 24);
        return;
    }

    int w, h;
    glfwGetFramebufferSize(win, &w, &h);

    unsigned char *pixels = malloc((size_t)(w * h * 4));
    if (!pixels) {
        ws_send_text(client_fd, "{\"error\":\"malloc failed\"}", 24);
        return;
    }

    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    /* Vertikale Spiegelung (OpenGL ist bottom-up) */
    int row_bytes = w * 4;
    unsigned char *row = malloc((size_t)row_bytes);
    for (int y = 0; y < h / 2; y++) {
        memcpy(row, pixels + y * row_bytes, (size_t)row_bytes);
        memcpy(pixels + y * row_bytes, pixels + (h - 1 - y) * row_bytes, (size_t)row_bytes);
        memcpy(pixels + (h - 1 - y) * row_bytes, row, (size_t)row_bytes);
    }
    free(row);

    /* Pfad zusammenbauen */
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/screenshot_%ld.png",
             path, (long)time(NULL));

    int ok = stbi_write_png(filepath, w, h, 4, pixels, w * 4);
    free(pixels);

    char resp[1200];
    if (ok)
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"path\":\"%s\",\"width\":%d,\"height\":%d}",
                 filepath, w, h);
    else
        snprintf(resp, sizeof(resp), "{\"error\":\"write failed\"}");
    ws_send_text(client_fd, resp, strlen(resp));
}

/* ---- Navigate Command ---- */

static void cmd_navigate(int client_fd, const char *url)
{
    if (!url || url[0] == '\0') {
        const char *err = "{\"error\":\"missing url\"}";
        ws_send_text(client_fd, err, strlen(err));
        return;
    }

    free(pending_navigate_url);
    pending_navigate_url = strdup(url);
    pending_navigate_client_fd = client_fd;

    /* Response wird gesendet nachdem main.c die Navigation durchgefuehrt hat */
}

/* ---- JSON String-Feld extrahieren ---- */

static int json_extract_string(const char *msg, const char *key,
                               char *out, size_t out_size)
{
    const char *p = strstr(msg, key);
    if (!p) return -1;
    p = strchr(p + strlen(key), '"');
    if (!p) return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* ---- Message Handler ---- */

static void handle_message(int client_fd, const char *msg)
{
    /* Einfaches JSON-Parsing: {"command":"screenshot","path":"/tmp"} */
    const char *cmd = strstr(msg, "\"command\"");
    if (!cmd) {
        const char *err = "{\"error\":\"missing command\"}";
        ws_send_text(client_fd, err, strlen(err));
        return;
    }

    if (strstr(msg, "\"screenshot\"")) {
        char path[512] = "/tmp";
        json_extract_string(msg, "\"path\"", path, sizeof(path));
        cmd_screenshot(client_fd, path);
    } else if (strstr(msg, "\"navigate\"")) {
        char url[2048] = "";
        json_extract_string(msg, "\"url\"", url, sizeof(url));
        cmd_navigate(client_fd, url);
    } else {
        const char *err = "{\"error\":\"unknown command\"}";
        ws_send_text(client_fd, err, strlen(err));
    }
}

/* ---- WebSocket Handshake ---- */

static int do_handshake(DebugClient *c)
{
    /* Finde Sec-WebSocket-Key */
    const char *key_hdr = strstr(c->recv_buf, "Sec-WebSocket-Key: ");
    if (!key_hdr) return -1;
    key_hdr += 19;
    const char *key_end = strstr(key_hdr, "\r\n");
    if (!key_end) return -1;

    size_t key_len = (size_t)(key_end - key_hdr);
    char key_plus_magic[256];
    snprintf(key_plus_magic, sizeof(key_plus_magic), "%.*s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
             (int)key_len, key_hdr);

    unsigned char hash[20];
    sha1((const unsigned char *)key_plus_magic, strlen(key_plus_magic), hash);

    char accept[64];
    base64_encode(hash, 20, accept);

    char response[512];
    int rlen = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);

    write(c->fd, response, (size_t)rlen);
    c->upgraded = 1;
    c->recv_len = 0;

    printf("[debug] WebSocket client connected\n");
    return 0;
}

/* ---- Public API ---- */

int debug_server_start(int port)
{
    debug_port = port;
    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].fd = -1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("debug socket");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Non-blocking */
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("debug bind");
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    if (listen(server_fd, 4) < 0) {
        perror("debug listen");
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    printf("[debug] WebSocket server listening on ws://127.0.0.1:%d\n", port);
    return 0;
}

void debug_server_poll(void)
{
    if (server_fd < 0) return;

    /* Accept new connections */
    for (;;) {
        int fd = accept(server_fd, NULL, NULL);
        if (fd < 0) break;

        fcntl(fd, F_SETFL, O_NONBLOCK);

        int placed = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd < 0) {
                clients[i].fd = fd;
                clients[i].upgraded = 0;
                clients[i].recv_len = 0;
                placed = 1;
                break;
            }
        }
        if (!placed) {
            close(fd);
        }
    }

    /* Read from clients */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        DebugClient *c = &clients[i];
        if (c->fd < 0) continue;

        ssize_t n = read(c->fd, c->recv_buf + c->recv_len,
                         sizeof(c->recv_buf) - c->recv_len - 1);
        if (n == 0) {
            /* Disconnected */
            close(c->fd);
            c->fd = -1;
            printf("[debug] Client disconnected\n");
            continue;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            close(c->fd);
            c->fd = -1;
            continue;
        }

        c->recv_len += (size_t)n;
        c->recv_buf[c->recv_len] = '\0';

        if (!c->upgraded) {
            /* HTTP upgrade handshake */
            if (strstr(c->recv_buf, "\r\n\r\n")) {
                if (do_handshake(c) < 0) {
                    close(c->fd);
                    c->fd = -1;
                }
            }
        } else {
            /* WebSocket frames */
            while (c->recv_len > 0) {
                char msg[MAX_MSG_SIZE];
                size_t frame_len = 0;
                int opcode = ws_decode_frame(
                    (unsigned char *)c->recv_buf, c->recv_len,
                    msg, sizeof(msg), &frame_len);

                if (opcode < 0) break; /* Incomplete frame */

                if (opcode == 0x08) {
                    /* Close */
                    close(c->fd);
                    c->fd = -1;
                    printf("[debug] Client closed\n");
                    break;
                }

                if (opcode == 0x01) {
                    /* Text message */
                    handle_message(c->fd, msg);
                }

                /* Consume frame */
                memmove(c->recv_buf, c->recv_buf + frame_len,
                        c->recv_len - frame_len);
                c->recv_len -= frame_len;
            }
        }
    }
}

char *debug_server_consume_navigate(void)
{
    if (!pending_navigate_url) return NULL;

    char *url = pending_navigate_url;
    pending_navigate_url = NULL;

    /* Bestaetigung an den Client senden */
    if (pending_navigate_client_fd >= 0) {
        char resp[2200];
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"url\":\"%s\"}", url);
        ws_send_text(pending_navigate_client_fd, resp, strlen(resp));
        pending_navigate_client_fd = -1;
    }

    return url;
}

void debug_server_stop(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            close(clients[i].fd);
            clients[i].fd = -1;
        }
    }
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    free(pending_navigate_url);
    pending_navigate_url = NULL;
}
