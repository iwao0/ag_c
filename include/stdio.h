#ifndef _STDIO_H
#define _STDIO_H

/* NOTE: C11 標準では FILE * を使うが、現パーサーが typedef 名の
 * 引数型・戻り値型に未対応のため void * で代替している。
 * また const 修飾も省略している。 */

#define NULL ((void *)0)
#define EOF  (-1)

/* Formatted output */
int printf(char *fmt, ...);
int fprintf(void *stream, char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int snprintf(char *buf, long size, char *fmt, ...);

/* Character output */
int puts(char *s);
int putchar(int c);
int fputs(char *s, void *stream);
int fputc(int c, void *stream);

/* Character input */
int getchar(void);
int fgetc(void *stream);
char *fgets(char *s, int size, void *stream);

/* File operations */
void *fopen(char *path, char *mode);
int fclose(void *stream);
int fflush(void *stream);

/* Binary I/O */
long fread(void *ptr, long size, long nmemb, void *stream);
long fwrite(void *ptr, long size, long nmemb, void *stream);

/* Error */
void perror(char *s);

#endif
