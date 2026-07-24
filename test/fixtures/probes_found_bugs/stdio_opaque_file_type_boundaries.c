// stdio.h/wchar.h shared opaque FILE type and stream API boundaries.
// Expected: exit=0
#include <wchar.h>
#include <stdio.h>

static FILE *(*tmpfile_signature)(void) = tmpfile;
static int (*fclose_signature)(FILE *) = fclose;
static int (*fwide_signature)(FILE *, int) = fwide;

int main(void) {
    volatile int file_pointer_kind = _Generic(
        (FILE *)0,
        FILE *: 1,
        void *: 2,
        default: 0);
    FILE *stream;

    if (file_pointer_kind != 1 || sizeof(FILE *) != sizeof(void *))
        return 1;
    if (!tmpfile_signature || !fclose_signature || !fwide_signature)
        return 2;

    stream = tmpfile_signature();
    if (!stream)
        return 0;
    (void)fwide_signature(stream, 0);
    if (fclose_signature(stream) != 0)
        return 3;
    return 0;
}
