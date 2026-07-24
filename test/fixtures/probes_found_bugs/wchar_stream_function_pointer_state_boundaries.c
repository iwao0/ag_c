#include <stdio.h>
#include <wchar.h>

static wint_t (*fgetwc_signature)(FILE *) = fgetwc;
static wint_t (*getwc_signature)(FILE *) = getwc;
static wint_t (*getwchar_signature)(void) = getwchar;
static wint_t (*fputwc_signature)(wchar_t, FILE *) = fputwc;
static wint_t (*putwc_signature)(wchar_t, FILE *) = putwc;
static wint_t (*putwchar_signature)(wchar_t) = putwchar;
static wint_t (*ungetwc_signature)(wint_t, FILE *) = ungetwc;
static wchar_t *(*fgetws_signature)(wchar_t *, int, FILE *) = fgetws;
static int (*fputws_signature)(const wchar_t *, FILE *) = fputws;
static int (*fwide_signature)(FILE *, int) = fwide;

int main(void) {
    FILE *stream;

    if (!fgetwc_signature || !getwc_signature || !getwchar_signature ||
        !fputwc_signature || !putwc_signature || !putwchar_signature ||
        !ungetwc_signature || !fgetws_signature || !fputws_signature ||
        !fwide_signature) {
        return 1;
    }

    stream = tmpfile();
    if (!stream) {
        /*
         * Standalone WAT has no file store.  Keep its explicit wide-I/O stub
         * contract covered through the same exact function-pointer types.
         */
        FILE *stub_stream = (FILE *)1;
        wchar_t buffer[3] = {L'K', L'\0', L'R'};

        if (fwide_signature(0, 1) != 0) return 2;
        if (fwide_signature(stub_stream, 1) != 1) return 3;
        if (fwide_signature(stub_stream, -1) != 1 ||
            fwide_signature(stub_stream, 0) != 1) {
            return 4;
        }
        if (fputwc_signature(L'A', stub_stream) != L'A') return 5;
        if (putwc_signature(L'B', stub_stream) != L'B') return 6;
        if (putwchar_signature(L'C') != L'C') return 7;
        if (fputws_signature(L"wide", stub_stream) != 4) return 8;
        if (fputwc_signature(L'X', 0) != WEOF ||
            putwc_signature(L'Y', 0) != WEOF ||
            fputws_signature(L"wide", 0) != WEOF) {
            return 9;
        }
        if (fgetwc_signature(stub_stream) != WEOF ||
            getwc_signature(stub_stream) != WEOF ||
            getwchar_signature() != WEOF) {
            return 10;
        }
        if (ungetwc_signature(L'Q', stub_stream) != WEOF) return 11;
        if (fgetws_signature(buffer, 3, stub_stream) != 0 ||
            buffer[0] != L'K' || buffer[1] != L'\0' ||
            buffer[2] != L'R') {
            return 12;
        }
        return 0;
    }

    if (fwide_signature(stream, 0) != 0) return 13;
    if (fwide_signature(stream, 1) <= 0) return 14;
    if (fwide_signature(stream, -1) <= 0 ||
        fwide_signature(stream, 0) <= 0) {
        return 15;
    }

    if (fputwc_signature(L'A', stream) != L'A') return 16;
    if (putwc_signature(L'B', stream) != L'B') return 17;
    if (fputwc_signature(L'\n', stream) != L'\n') return 18;
    if (fputws_signature(L"CD\n", stream) < 0) return 19;

    rewind(stream);
    if (fwide_signature(stream, 0) <= 0) return 20;
    if (fgetwc_signature(stream) != L'A') return 21;
    if (ungetwc_signature(L'Z', stream) != L'Z') return 22;
    if (getwc_signature(stream) != L'Z') return 23;
    if (getwc_signature(stream) != L'B') return 24;

    {
        wchar_t line[4] = {L'K', L'K', L'K', L'R'};
        if (fgetws_signature(line, 4, stream) != line) return 25;
        if (line[0] != L'\n' || line[1] != L'\0' ||
            line[2] != L'K' || line[3] != L'R') {
            return 26;
        }
        if (fgetws_signature(line, 4, stream) != line) return 27;
        if (line[0] != L'C' || line[1] != L'D' ||
            line[2] != L'\n' || line[3] != L'\0') {
            return 28;
        }
    }

    if (fgetwc_signature(stream) != WEOF || !feof(stream) || ferror(stream)) {
        return 29;
    }
    if (ungetwc_signature(WEOF, stream) != WEOF) return 30;
    clearerr(stream);
    if (feof(stream) || ferror(stream)) return 31;

    rewind(stream);
    if (fgetwc_signature(stream) != L'A') return 32;
    if (fclose(stream) != 0) return 33;
    return 0;
}
