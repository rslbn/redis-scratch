#include "utils.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include "server.h"

void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

void msg_errno(const char *msg) {
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);;
}

void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    std::abort();
}


int32_t read_full(int fd, uint8_t *buf, size_t n) {
    while(n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1; // error, or unexpected EOF 
        }
        assert((size_t)rv <= n);

        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

int32_t write_all(int fd, const uint8_t *buf, size_t n) { 
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1; //error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}