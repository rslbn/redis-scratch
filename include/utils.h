#ifndef UTILS_H
#define UTILS_H
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <cerrno>

void msg(const char *msg);
void msg_errno(const char *msg);
void die(const char *msg);

int32_t read_full(int connfd, uint8_t *buf, size_t n);
int32_t write_all(int connfd, const uint8_t *buf, size_t n);
#endif