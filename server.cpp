#include "utils.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>


// static void do_something(int connfd) {
//     char rbuf[64] = {};
//     ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
//     if (n < 0) {
//         msg("read() error");
//         return;
//     }
//     fprintf(stderr, "client says: %s\n", rbuf);
//     char wbuf[] = "world";
//     write(connfd, wbuf, strlen(wbuf));
// }


static int32_t one_request(int connfd) {
    char rbuf[4 + K_MAX_MSG];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }
    uint32_t len = 0;
    memcpy(&len, rbuf, sizeof(uint32_t));
    if (len > K_MAX_MSG) {
        msg("too long");
        return -1;
    }

    // request body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // do something
    printf("client says: %.*s\n", len, &rbuf[4]);
    
    // reply using the same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    // this is needed for most server applications
    int val = 1; // related to TIME_WAIT in tcp
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); // port
    addr.sin_addr.s_addr = htonl(0); // wildcard address 0.0.0.0
    int rv = bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
    if (rv) {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }

    while(true) {
        // accept
        struct sockaddr_in client_addr = {};
        socklen_t addr_len = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *) &client_addr, &addr_len);
        if (connfd < 0) {
            continue; // error
        }
        // only serves one client connection at once
        while(true) {
            int32_t err = one_request(connfd);
            if (err) break;
        }
        close(connfd);
    }
    return 0;
}