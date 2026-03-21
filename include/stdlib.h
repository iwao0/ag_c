#ifndef _STDLIB_H
#define _STDLIB_H

/* NOTE: C11 標準では size_t を使うが、現パーサーが typedef 名の
 * 引数型・戻り値型に未対応のため long で代替している。
 * また const 修飾も省略している。 */

#define NULL ((void *)0)
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* Memory management */
void *malloc(long size);
void *calloc(long nmemb, long size);
void *realloc(void *ptr, long size);
void free(void *ptr);

/* Process control */
void exit(int status);
void abort(void);
int atexit(void *func);

/* String conversion */
int atoi(char *s);
long atol(char *s);
long strtol(char *s, char **endptr, int base);

/* Pseudo-random numbers */
int rand(void);
void srand(int seed);

/* Integer arithmetic */
int abs(int n);
long labs(long n);

/* Sorting and searching */
void qsort(void *base, long nmemb, long size, void *compar);
void *bsearch(void *key, void *base, long nmemb, long size, void *compar);

/* Environment */
char *getenv(char *name);
int system(char *command);

#endif
