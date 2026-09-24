#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include "../include/utils.h"
#include "../include/enums.h"

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
    uint32_t len = 4;
    for (const std::string &s: cmd) {
        len += 4 + s.size();
    }
    if (len > K_MAX_MSG) {
        return -1;
    }

    char wbuf[(K_MAX_MSG) + 4]; 
    memcpy(&wbuf[0], &len, 4); // assume little endian
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t cur = 8;
    for (const std::string &s : cmd) {
        uint32_t p = (uint32_t) s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }
    return write_all(fd, (const uint8_t *)&wbuf[0], 4 + len);
}

static int32_t print_response(const uint8_t *data, size_t size) {
    if (size < 1) {
        msg("bad response");
        return -1;
    }

    switch(data[0]) {
        case TAG_NIL:
            printf("(nil)\n");
            return 1;
        case TAG_ERR:
            if (size < 1 + 8) {
                msg("bad response");
                return -1;
            }
            {
                int32_t code = 0;
                uint32_t len = 0;
                memcpy(&code, &data[1], 4);
                memcpy(&len, &data[1 + 4], 4);
                if (size < 1 + 8 + len) {
                    msg("bad response");
                    return -1;
                }
                printf("(err) %d %.*s\n", code, len, &data[1 + 8]);
                return 1 + 8 + len;
            }
        case TAG_STR:
            if (size < 1 + 4) {
                msg("bad response");
                return -1;
            }
            {
                uint32_t len = 0;
                memcpy(&len, &data[1], 4);
                if (size < 1 + 4 + len) {
                    msg("bad response");
                    return -1;
                }
                printf("(str) %.*s\n", len, &data[1 + 4]);
                return 1 + 4 + len;
            }
        case TAG_INT:
            if (size < 1 + 8) {
                msg("bad response");
                return -1;
            }
            {
                int64_t val = 0;
                memcpy(&val, &data[1], 8);
                printf("(int) %ld\n", val);
                return 1 + 8;
            }
        case TAG_DBL:
            if (size < 1 + 8) {
                msg("bad response");
                return -1;
            }
            {
                double val = 0;
                memcpy(&val, &data[1], 8);
                printf("(dbl) %g\n", val);
                return 1 + 8;
            }
        case TAG_ARR:
            if (size < 1 + 4) {
                msg("bad response");
                return -1;
            }
            {
                uint32_t len = 0;
                memcpy(&len, &data[1], 4);
                printf("(arr) len = %u\n", len);
                size_t arr_bytes = 1 + 4;
                for (uint32_t i = 0; i < len; i++) {
                    int32_t rv = print_response(&data[arr_bytes], size - arr_bytes);
                    if (rv < 0) {
                        return rv;
                    }
                    arr_bytes += (size_t)rv;
                }
                printf("(arr) end\n");
                return (int32_t)arr_bytes;
            }
        default:
            msg("bad response");
            return -1;
    }
}

static int32_t read_res(int fd) {

    char rbuf[(K_MAX_MSG) + 4];
    errno = 0;
    int32_t err = read_full(fd, (uint8_t*)&rbuf[0], 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);

    if (len > K_MAX_MSG) {
        msg("too long");
        return -1;
    }

    // reply body
    err = read_full(fd, (uint8_t *)&rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // print the result
    int32_t rv = print_response((uint8_t *)&rbuf[4], len);
    if (rv > 0 && (uint32_t)rv != len) {
        msg("bad response");
        rv = -1;
    }
    return rv;
}


int main(int argc, char **argv) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    const char *server_ip = (argc > 1) ? argv[1] : "127.0.0.1";

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        die("invalid IP address");
    }

    int rv = connect(fd, (const struct sockaddr *) &addr, sizeof(addr));
    if (rv) {
        die("connect");
    }

    std::vector<std::string> cmd;
    for (int i = 2; i < argc; i++) {
        cmd.push_back(argv[i]);
    }

    int32_t err = send_req(fd, cmd);
    if (err) {
        goto L_DONE;
    }
    err = read_res(fd);
    if (err) {
        goto L_DONE;
    }
    L_DONE:
        close(fd);
    return 0;
}
