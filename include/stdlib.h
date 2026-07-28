#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define NULL ((void *)0)
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#ifdef __wasm32__
#define RAND_MAX 32767
#define MB_CUR_MAX 4
#else
#define RAND_MAX 0x7fffffff
extern int __mb_cur_max;
#define MB_CUR_MAX __mb_cur_max
#endif

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

/* Memory management */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *aligned_alloc(size_t alignment, size_t size);

/* Process control */
_Noreturn void exit(int status);
_Noreturn void abort(void);
_Noreturn void _Exit(int status);
int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
_Noreturn void quick_exit(int status);

/* String conversion */
int atoi(const char *s);
double atof(const char *s);
long atol(const char *s);
long long atoll(const char *s);
long strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
long long strtoll(const char *s, char **endptr, int base);
unsigned long long strtoull(const char *s, char **endptr, int base);
float strtof(const char *s, char **endptr);
double strtod(const char *s, char **endptr);
long double strtold(const char *s, char **endptr);

/* Multibyte / wide-character conversion */
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *dst, const char *src, size_t n);
size_t wcstombs(char *dst, const wchar_t *src, size_t n);

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
char *getenv(const char *name);
int system(const char *command);
char *realpath(const char *path, char *resolved_path);

#endif
