#ifndef _STRING_H
#define _STRING_H

/* NOTE: size_t 等の typedef 名を戻り値型に使うと funcdef() で
 * 型解決できないため、long で代替している。 */

/* Copying */
void *memcpy(void *dest, const void *src, long n);
void *memmove(void *dest, const void *src, long n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, long n);

/* Concatenation */
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, long n);

/* Comparison */
int memcmp(const void *s1, const void *s2, long n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, long n);

/* Searching */
void *memchr(const void *s, int c, long n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *str, const char *delim);

/* Other */
void *memset(void *s, int c, long n);
long strlen(const char *s);
char *strerror(int errnum);

#endif
