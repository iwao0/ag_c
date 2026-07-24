#ifndef _WCHAR_H
#define _WCHAR_H
#include <stddef.h>
#include <stdarg.h>

#ifndef _WINT_T
#define _WINT_T
typedef int wint_t;
#endif
#ifndef __MBSTATE_T_DEFINED
#define __MBSTATE_T_DEFINED
typedef struct { unsigned int __o[32 / sizeof(unsigned int)]; } mbstate_t;
#endif
struct tm;
#ifndef FILE
#ifndef _FILE_T
#define _FILE_T
typedef void FILE;
#endif
#endif

#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif
#ifndef WCHAR_MIN
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647
#endif

/* 文字列 */
size_t wcslen(const wchar_t *s);
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src);
wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wcscat(wchar_t *dst, const wchar_t *src);
wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n);
int wcscmp(const wchar_t *s1, const wchar_t *s2);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
int wcscoll(const wchar_t *s1, const wchar_t *s2);
size_t wcsxfrm(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *s, const wchar_t *sub);
size_t wcsspn(const wchar_t *s, const wchar_t *accept);
size_t wcscspn(const wchar_t *s, const wchar_t *reject);
wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept);
wchar_t *wcstok(wchar_t *s, const wchar_t *delim, wchar_t **ptr);
/* メモリ */
wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);
/* 変換 */
long wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base);
float wcstof(const wchar_t *nptr, wchar_t **endptr);
double wcstod(const wchar_t *nptr, wchar_t **endptr);
long double wcstold(const wchar_t *nptr, wchar_t **endptr);
/* マルチバイト */
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
int mbsinit(const mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);
size_t wcsftime(wchar_t *wcs, size_t maxsize, const wchar_t *format, const struct tm *timeptr);
/* I/O (可変長) */
int fwprintf(FILE *stream, const wchar_t *fmt, ...);
int wprintf(const wchar_t *fmt, ...);
int swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...);
int vfwprintf(FILE *stream, const wchar_t *fmt, va_list arg);
int vwprintf(const wchar_t *fmt, va_list arg);
int vswprintf(wchar_t *s, size_t n, const wchar_t *fmt, va_list arg);
int fwscanf(FILE *stream, const wchar_t *fmt, ...);
int wscanf(const wchar_t *fmt, ...);
int swscanf(const wchar_t *s, const wchar_t *fmt, ...);
int vfwscanf(FILE *stream, const wchar_t *fmt, va_list arg);
int vwscanf(const wchar_t *fmt, va_list arg);
int vswscanf(const wchar_t *s, const wchar_t *fmt, va_list arg);
wint_t fgetwc(FILE *stream);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t fputwc(wchar_t wc, FILE *stream);
wint_t putwc(wchar_t wc, FILE *stream);
wint_t putwchar(wchar_t wc);
wint_t ungetwc(wint_t wc, FILE *stream);
wchar_t *fgetws(wchar_t *s, int n, FILE *stream);
int fputws(const wchar_t *s, FILE *stream);
int fwide(FILE *stream, int mode);
wint_t btowc(int c);
int wctob(wint_t c);
#endif
