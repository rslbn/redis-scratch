#include "utils.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static int32_t send_req(int fd, const uint8_t *text, size_t len) {
    if (len > K_MAX_MSG) {
        return -1;
    }

    std::vector<uint8_t> wbuf;
    buf_append(wbuf, (const uint8_t*)&len, 4);
    buf_append(wbuf, text, len);
    return write_all(fd, wbuf.data(), wbuf.size());
}

static int32_t read_res(int fd) {
    std::vector<uint8_t> rbuf;
    rbuf.resize(4);
    errno = 0;

    int32_t err = read_full(fd, &rbuf[0], 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf.data(), 4);

    if (len > K_MAX_MSG) {
        msg("too long");
        return -1;
    }

    // reply body
    rbuf.resize(4 + len);
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // do something
    printf("len:%u data:%.*s\n", len, len < 100 ? len : 100, &rbuf[4]);
    return 0;
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

    std::vector<std::string> query_list = {
        "hello", "hello2", "hello3",
        std::string(K_MAX_MSG, 'z'),
        "hello5"
    };

    for (const std::string &s : query_list) {
        int32_t err = send_req(fd, (uint8_t *)s.data(), s.size());
        if (err) {
            goto L_DONE;
        }
    }

    for (size_t i = 0; i < query_list.size(); i++) {
        int32_t err = read_res(fd);
        if (err) {
            goto L_DONE;
        }
    }

    L_DONE:
        close(fd);
    return 0;
}
