#ifndef AGC_STRING_H
#define AGC_STRING_H

/* Copying */
void *memcpy();
void *memmove();
char *strcpy();
char *strncpy();

/* Concatenation */
char *strcat();
char *strncat();

/* Comparison */
int memcmp();
int strcmp();
int strncmp();

/* Searching */
void *memchr();
char *strchr();
char *strrchr();
char *strstr();
char *strtok();

/* Other */
void *memset();
long strlen();
char *strerror();

#endif
