#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifndef _FILE_T
#define _FILE_T
#ifdef __wasm32__
typedef struct __agc_FILE FILE;
#else
/* Match Darwin's opaque stdio tag without exposing its private layout. */
typedef struct __sFILE FILE;
#endif
#endif
#ifdef __wasm32__
typedef long fpos_t;
#else
/* Apple SDK's __darwin_off_t is the compiler's signed 64-bit long long. */
typedef long long fpos_t;
#endif

#define EOF  (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#ifdef __wasm32__
#define BUFSIZ 8192
#define FOPEN_MAX 11
#define FILENAME_MAX 64
#define L_tmpnam 32
#define TMP_MAX 10000
#else
#define BUFSIZ 1024
#define FOPEN_MAX 20
#define FILENAME_MAX 1024
#define L_tmpnam 1024
#define TMP_MAX 308915776
#endif
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

/* Formatted output */
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* Formatted input */
int scanf(const char *fmt, ...);
int vscanf(const char *fmt, va_list ap);
int fscanf(FILE *stream, const char *fmt, ...);
int vfscanf(FILE *stream, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);
int vsscanf(const char *s, const char *fmt, va_list ap);

/* Character output */
int puts(const char *s);
int putchar(int c);
int fputs(const char *s, FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);

/* Character input */
int getchar(void);
int fgetc(FILE *stream);
int getc(FILE *stream);
int ungetc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
long getline(char **lineptr, size_t *n, FILE *stream);

/* File operations */
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
FILE *tmpfile(void);
char *tmpnam(char *s);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
int fflush(FILE *stream);
void setbuf(FILE *stream, char *buf);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);
void rewind(FILE *stream);

/* Binary I/O */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

/* Error */
void perror(const char *s);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

/* Standard streams (Apple libc が __std{in,out,err}p としてエクスポート、stdio.h は
 * 通常マクロで包む)。fprintf(stderr, ...) 等を使えるよう、同じ規約で extern 宣言する。
 * codegen は is_extern_decl のグローバル変数を @GOTPAGE 経由でリンクする。 */
extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;
#define stdin  __stdinp
#define stdout __stdoutp
#define stderr __stderrp

#endif
