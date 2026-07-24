#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int bytes_equal(const unsigned char *left,
                       const unsigned char *right,
                       unsigned long count) {
    unsigned long index;
    for (index = 0; index < count; index++) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

int main(void) {
    int (*open_fn)(const char *, int, ...) = open;
    long (*read_fn)(int, void *, unsigned long) = read;
    long (*write_fn)(int, const void *, unsigned long) = write;
    long (*lseek_fn)(int, long, int) = lseek;
    int (*close_fn)(int) = close;
    int (*fstat_fn)(int, struct stat *) = fstat;
    unsigned char buffer[10] = {
        0xa1, 0xa2, 0xa3, 0xa4, 0xa5,
        0xa6, 0xa7, 0xa8, 0xa9, 0xaa
    };
    struct stat status = {0};
    char path[L_tmpnam];
    char *name;
    int fd;

    if (!open_fn || !read_fn || !write_fn || !lseek_fn ||
        !close_fn || !fstat_fn) {
        return 1;
    }
#ifdef __wasm32__
    if (sizeof(struct stat) != 16 || L_tmpnam != 32) return 29;
#else
    if (sizeof(struct stat) != 144 || L_tmpnam != 1024) return 29;
#endif

    name = tmpnam(path);
    if (!name) name = "agc_posix_fd_unavailable";
    remove(name);
    fd = open_fn(name, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) {
        /*
         * Standalone WAT deliberately has no persistent file store.  Its
         * POSIX fd stubs must fail without changing caller-owned storage.
         */
        status.st_mode = (mode_t)01234;
        status.st_size = 5678;
        if (open_fn("missing", O_RDONLY) != -1) return 2;
        if (read_fn(-1, buffer + 1, 4) != -1) return 3;
        if (write_fn(-1, buffer + 1, 4) != -1) return 4;
        if (lseek_fn(-1, 3, SEEK_SET) != -1) return 5;
        if (fstat_fn(-1, &status) != -1) return 6;
        if (close_fn(-1) != -1) return 7;
        if (buffer[0] != 0xa1 || buffer[1] != 0xa2 ||
            buffer[5] != 0xa6 || status.st_mode != (mode_t)01234 ||
            status.st_size != 5678) {
            return 8;
        }
        return 0;
    }

    {
        const unsigned char initial[] = {'A', 0, 0x80, 'Z'};
        if (write_fn(fd, initial, sizeof(initial)) != (long)sizeof(initial)) {
            return 9;
        }
    }

    if (fstat_fn(fd, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size != 4) {
        return 10;
    }

    if (lseek_fn(fd, 1, SEEK_SET) != 1) return 11;
    if (read_fn(fd, buffer + 1, 2) != 2) return 12;
    if (buffer[0] != 0xa1 || buffer[1] != 0 ||
        buffer[2] != 0x80 || buffer[3] != 0xa4) {
        return 13;
    }

    if (lseek_fn(fd, 2, SEEK_END) != 6) return 14;
    if (write_fn(fd, "Q", 1) != 1) return 15;
    if (fstat_fn(fd, &status) != 0 || status.st_size != 7) return 16;
    if (lseek_fn(fd, 0, SEEK_SET) != 0) return 17;
    if (read_fn(fd, buffer + 1, 7) != 7) return 18;
    {
        const unsigned char expected[] = {'A', 0, 0x80, 'Z', 0, 0, 'Q'};
        if (buffer[0] != 0xa1 || buffer[8] != 0xa9 ||
            !bytes_equal(buffer + 1, expected, sizeof(expected))) {
            return 19;
        }
    }

    if (close_fn(fd) != 0 || close_fn(fd) != -1) return 20;
    if (read_fn(fd, buffer, 1) != -1 ||
        write_fn(fd, "x", 1) != -1 ||
        lseek_fn(fd, 0, SEEK_SET) != -1 ||
        fstat_fn(fd, &status) != -1) {
        return 21;
    }

    fd = open_fn(name, O_WRONLY | O_APPEND);
    if (fd < 0) return 22;
    if (lseek_fn(fd, 0, SEEK_SET) != 0 ||
        write_fn(fd, "R", 1) != 1 ||
        close_fn(fd) != 0) {
        return 23;
    }

    if (open_fn(name, O_CREAT | O_EXCL | O_RDWR, 0600) != -1) return 24;

    fd = open_fn(name, O_RDONLY);
    if (fd < 0) return 25;
    if (read_fn(fd, buffer + 1, 8) != 8 || close_fn(fd) != 0) return 26;
    {
        const unsigned char expected[] = {'A', 0, 0x80, 'Z', 0, 0, 'Q', 'R'};
        if (!bytes_equal(buffer + 1, expected, sizeof(expected))) return 27;
    }

    if (remove(name) != 0) return 28;
    return 0;
}
