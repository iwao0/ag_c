#ifndef _INTTYPES_H
#define _INTTYPES_H
#include <stdint.h>
/* C11 7.8: 整数型の printf/scanf 書式マクロと最大幅整数の関数。
 * Apple ARM64: int32_t=int, int64_t=long long, intmax_t=long long。 */
typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;

#define PRId8   "d"
#define PRId16  "d"
#define PRId32  "d"
#define PRId64  "lld"
#define PRIdMAX "lld"
#define PRIi8   "i"
#define PRIi16  "i"
#define PRIi32  "i"
#define PRIi64  "lli"
#define PRIiMAX "lli"
#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  "u"
#define PRIu64  "llu"
#define PRIuMAX "llu"
#define PRIo64  "llo"
#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  "x"
#define PRIx64  "llx"
#define PRIxMAX "llx"
#define PRIX64  "llX"
#define PRIXMAX "llX"
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#define PRIxPTR "lx"

#define SCNd32  "d"
#define SCNd64  "lld"
#define SCNu32  "u"
#define SCNu64  "llu"
#define SCNx32  "x"
#define SCNx64  "llx"

intmax_t imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);
#endif
