// POSIX offset/count type identity and wide seek-value boundaries.
// Expected: exit=0
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static ssize_t (*read_signature)(int, void *, size_t) = read;
static ssize_t (*write_signature)(int, const void *, size_t) = write;
static off_t (*lseek_signature)(int, off_t, int) = lseek;
static int (*fstat_signature)(int, struct stat *) = fstat;

int main(void) {
    volatile int off_kind = _Generic(
        (off_t)0,
        long: 1,
        long long: 2,
        default: 0);
    volatile int block_kind = _Generic(
        (blkcnt_t)0,
        long: 1,
        long long: 2,
        default: 0);
    volatile int count_kind = _Generic(
        (ssize_t)0,
        long: 1,
        default: 0);
    volatile int size_member_kind = _Generic(
        ((struct stat *)0)->st_size,
        long: 1,
        long long: 2,
        default: 0);
    const off_t wide_offset = (off_t)0x100000003LL;
    char path[L_tmpnam];
    char *name;
    int fd;

    if (!read_signature || !write_signature || !lseek_signature ||
        !fstat_signature)
        return 1;
    if (sizeof(off_t) != 8 || _Alignof(off_t) != 8 ||
        sizeof(blkcnt_t) != 8 || _Alignof(blkcnt_t) != 8 ||
        sizeof(ssize_t) != 8 || _Alignof(ssize_t) != 8 ||
        count_kind != 1)
        return 2;
#ifdef __wasm32__
    if (off_kind != 1 || block_kind != 1 || size_member_kind != 1 ||
        sizeof(struct stat) != 16 ||
        offsetof(struct stat, st_size) != 8)
        return 3;
#else
    if (off_kind != 2 || block_kind != 2 || size_member_kind != 2 ||
        _Generic(((struct stat *)0)->st_blocks,
                 long long: 1,
                 default: 0) != 1 ||
        _Generic(((struct stat *)0)->st_qspare[0],
                 long long: 1,
                 default: 0) != 1 ||
        sizeof(struct stat) != 144 ||
        offsetof(struct stat, st_size) != 96 ||
        offsetof(struct stat, st_blocks) != 104 ||
        offsetof(struct stat, st_qspare) != 128)
        return 4;
#endif

    name = tmpnam(path);
    if (!name)
        name = "agc_posix_offset_unavailable";
    remove(name);
    fd = open(name, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) {
        if (lseek_signature(-1, wide_offset, SEEK_SET) != (off_t)-1)
            return 5;
        return 0;
    }
    if (lseek_signature(fd, wide_offset, SEEK_SET) != wide_offset)
        return 6;
#ifndef __wasm32__
    {
        struct stat status = {0};
        if (write_signature(fd, "Q", 1) != 1 ||
            fstat_signature(fd, &status) != 0 ||
            status.st_size != wide_offset + 1)
            return 7;
    }
#else
    if (lseek_signature(fd, 0, SEEK_SET) != 0)
        return 8;
#endif
    if (close(fd) != 0 || remove(name) != 0)
        return 9;
    return 0;
}
