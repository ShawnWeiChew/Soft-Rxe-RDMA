#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <unistd.h>

ssize_t io_write(int fd, uint8_t *buf, size_t len);
ssize_t io_read(int fd, uint8_t *buf, size_t len);

#endif