// sys/types.h public typedefs and native struct stat member identities.
// Expected: exit=0
#include <sys/types.h>
#include <sys/stat.h>

int main(void) {
    volatile int size_kind =
        _Generic((size_t)0, unsigned long: 1, default: 0);
    volatile int ssize_kind =
        _Generic((ssize_t)0, long: 1, default: 0);
    volatile int blksize_kind =
        _Generic((blksize_t)0, int: 1, default: 0);
    volatile int dev_kind =
        _Generic((dev_t)0, int: 1, default: 0);
    volatile int gid_kind =
        _Generic((gid_t)0, unsigned int: 1, default: 0);
    volatile int ino_kind =
        _Generic((ino_t)0, unsigned long long: 1, default: 0);
    volatile int mode_kind =
        _Generic((mode_t)0, unsigned short: 1, default: 0);
    volatile int nlink_kind =
        _Generic((nlink_t)0, unsigned short: 1, default: 0);
    volatile int pid_kind =
        _Generic((pid_t)0, int: 1, default: 0);
    volatile int uid_kind =
        _Generic((uid_t)0, unsigned int: 1, default: 0);

    if (size_kind != 1 || ssize_kind != 1 || blksize_kind != 1 ||
        dev_kind != 1 || gid_kind != 1 || ino_kind != 1 ||
        mode_kind != 1 || nlink_kind != 1 || pid_kind != 1 ||
        uid_kind != 1)
        return 1;
    if (sizeof(size_t) != 8 || sizeof(ssize_t) != 8 ||
        sizeof(blksize_t) != 4 || sizeof(dev_t) != 4 ||
        sizeof(gid_t) != 4 || sizeof(ino_t) != 8 ||
        sizeof(mode_t) != 2 || sizeof(nlink_t) != 2 ||
        sizeof(pid_t) != 4 || sizeof(uid_t) != 4)
        return 2;
#ifndef __wasm32__
    {
        volatile int stat_dev_kind = _Generic(
            ((struct stat *)0)->st_dev, dev_t: 1, default: 0);
        volatile int stat_nlink_kind = _Generic(
            ((struct stat *)0)->st_nlink, nlink_t: 1, default: 0);
        volatile int stat_ino_kind = _Generic(
            ((struct stat *)0)->st_ino, ino_t: 1, default: 0);
        volatile int stat_uid_kind = _Generic(
            ((struct stat *)0)->st_uid, uid_t: 1, default: 0);
        volatile int stat_gid_kind = _Generic(
            ((struct stat *)0)->st_gid, gid_t: 1, default: 0);
        volatile int stat_rdev_kind = _Generic(
            ((struct stat *)0)->st_rdev, dev_t: 1, default: 0);
        volatile int stat_blksize_kind = _Generic(
            ((struct stat *)0)->st_blksize, blksize_t: 1, default: 0);

        if (stat_dev_kind != 1 || stat_nlink_kind != 1 ||
            stat_ino_kind != 1 || stat_uid_kind != 1 ||
            stat_gid_kind != 1 || stat_rdev_kind != 1 ||
            stat_blksize_kind != 1)
            return 3;
    }
#endif
    return 0;
}
