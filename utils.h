#ifndef UTILS_H
#define UTILS_H
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <cerrno>

#define K_MAX_MSG 4096

void msg(const char *msg);
void die(const char *msg);

int32_t read_full(int connfd, char *buf, size_t n);
int32_t write_all(int connfd, const char *buf, size_t n);
#endif