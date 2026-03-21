#ifndef _STRING_H
#define _STRING_H

/* NOTE: C11 標準では多くの引数に const 修飾があるが、
 * 現パーサーが関数プロトタイプの const 引数型に未対応のため省略している。
 * また size_t 等の typedef 名を戻り値型に使うと funcdef() で
 * 型解決できないため、long で代替している。 */

/* Copying */
void *memcpy(void *dest, void *src, long n);
void *memmove(void *dest, void *src, long n);
char *strcpy(char *dest, char *src);
char *strncpy(char *dest, char *src, long n);

/* Concatenation */
char *strcat(char *dest, char *src);
char *strncat(char *dest, char *src, long n);

/* Comparison */
int memcmp(void *s1, void *s2, long n);
int strcmp(char *s1, char *s2);
int strncmp(char *s1, char *s2, long n);

/* Searching */
void *memchr(void *s, int c, long n);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
char *strstr(char *haystack, char *needle);
char *strtok(char *str, char *delim);

/* Other */
void *memset(void *s, int c, long n);
long strlen(char *s);
char *strerror(int errnum);

#endif
