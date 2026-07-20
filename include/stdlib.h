#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define NULL ((void *)0)
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

/* Memory management */
void *malloc(long size);
void *calloc(long nmemb, long size);
void *realloc(void *ptr, long size);
void free(void *ptr);
void *aligned_alloc(long alignment, long size);

/* Process control */
void exit(int status);
void abort(void);
void _Exit(int status);
int atexit(void *func);
int at_quick_exit(void *func);
void quick_exit(int status);

/* String conversion */
int atoi(char *s);
double atof(char *s);
long atol(char *s);
long long atoll(char *s);
long strtol(char *s, char **endptr, int base);
unsigned long strtoul(char *s, char **endptr, int base);
long long strtoll(char *s, char **endptr, int base);
unsigned long long strtoull(char *s, char **endptr, int base);
float strtof(char *s, char **endptr);
double strtod(char *s, char **endptr);
long double strtold(char *s, char **endptr);

/* Multibyte / wide-character conversion */
int mblen(char *s, long n);
int mbtowc(int *pwc, char *s, long n);
int wctomb(char *s, int wc);
long mbstowcs(int *dst, char *src, long n);
long wcstombs(char *dst, int *src, long n);

/* Pseudo-random numbers */
int rand(void);
void srand(unsigned int seed);

/* Integer arithmetic */
int abs(int n);
long labs(long n);
long long llabs(long long n);
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);

/* Sorting and searching */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

/* Environment */
char *getenv(char *name);
int system(const char *command);
char *realpath(const char *path, char *resolved_path);

#endif
