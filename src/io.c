#include "../include/io.h"
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

ssize_t io_read(int fd, uint8_t *buf, size_t len) {
    ssize_t total = 0, nr = 0;
    while (total < len && (nr = read(fd, &buf[total], len - total)) != 0) {
        if (nr < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        }

        total += nr;
    }

    return total;
}

ssize_t io_write(int fd, uint8_t *buf, size_t len) {
    ssize_t total = 0, nw = 0;
    while (total < len) {
        nw = write(fd, &buf[total], len - total);
        if (nw <= 0) {
            if (nw == -1 && errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        }

        total += nw;
    }

    return total;
}