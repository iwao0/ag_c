#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

typedef void FILE;

#define EOF  (-1)

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
char *fgets(char *s, int size, FILE *stream);

/* File operations */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);

/* Binary I/O */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

/* Error */
void perror(const char *s);

#endif
