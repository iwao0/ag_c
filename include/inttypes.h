#ifndef _INTTYPES_H
#define _INTTYPES_H
#include <stdint.h>
/* C11 7.8: integer format macros and greatest-width integer functions. */
typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;

#define PRId8   "hhd"
#define PRId16  "hd"
#define PRId32  "d"
#define PRId64  "lld"
#ifdef __wasm32__
#define PRIdMAX "lld"
#else
#define PRIdMAX "ld"
#endif
#define PRIdPTR "ld"
#define PRIi8   "hhi"
#define PRIi16  "hi"
#define PRIi32  "i"
#define PRIi64  "lli"
#ifdef __wasm32__
#define PRIiMAX "lli"
#else
#define PRIiMAX "li"
#endif
#define PRIiPTR "li"
#define PRIo8   "hho"
#define PRIo16  "ho"
#define PRIo32  "o"
#define PRIo64  "llo"
#ifdef __wasm32__
#define PRIoMAX "llo"
#else
#define PRIoMAX "lo"
#endif
#define PRIoPTR "lo"
#define PRIu8   "hhu"
#define PRIu16  "hu"
#define PRIu32  "u"
#define PRIu64  "llu"
#ifdef __wasm32__
#define PRIuMAX "llu"
#else
#define PRIuMAX "lu"
#endif
#define PRIuPTR "lu"
#define PRIx8   "hhx"
#define PRIx16  "hx"
#define PRIx32  "x"
#define PRIx64  "llx"
#ifdef __wasm32__
#define PRIxMAX "llx"
#else
#define PRIxMAX "lx"
#endif
#define PRIxPTR "lx"
#define PRIX8   "hhX"
#define PRIX16  "hX"
#define PRIX32  "X"
#define PRIX64  "llX"
#ifdef __wasm32__
#define PRIXMAX "llX"
#else
#define PRIXMAX "lX"
#endif
#define PRIXPTR "lX"

#define SCNd8   "hhd"
#define SCNd16  "hd"
#define SCNd32  "d"
#define SCNd64  "lld"
#ifdef __wasm32__
#define SCNdMAX "lld"
#else
#define SCNdMAX "ld"
#endif
#define SCNdPTR "ld"
#define SCNi8   "hhi"
#define SCNi16  "hi"
#define SCNi32  "i"
#define SCNi64  "lli"
#ifdef __wasm32__
#define SCNiMAX "lli"
#else
#define SCNiMAX "li"
#endif
#define SCNiPTR "li"
#define SCNo8   "hho"
#define SCNo16  "ho"
#define SCNo32  "o"
#define SCNo64  "llo"
#ifdef __wasm32__
#define SCNoMAX "llo"
#else
#define SCNoMAX "lo"
#endif
#define SCNoPTR "lo"
#define SCNu8   "hhu"
#define SCNu16  "hu"
#define SCNu32  "u"
#define SCNu64  "llu"
#ifdef __wasm32__
#define SCNuMAX "llu"
#else
#define SCNuMAX "lu"
#endif
#define SCNuPTR "lu"
#define SCNx8   "hhx"
#define SCNx16  "hx"
#define SCNx32  "x"
#define SCNx64  "llx"
#ifdef __wasm32__
#define SCNxMAX "llx"
#else
#define SCNxMAX "lx"
#endif
#define SCNxPTR "lx"

intmax_t imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);
#endif
