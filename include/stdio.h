#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

typedef void FILE;

#define EOF  (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Formatted output */
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);

/* Character output */
int puts(const char *s);
int putchar(int c);
int fputs(const char *s, FILE *stream);
int fputc(int c, FILE *stream);

/* Character input */
int getchar(void);
int fgetc(FILE *stream);
int getc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);

/* File operations */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
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
