#ifndef _INTTYPES_H
#define _INTTYPES_H
#include <stddef.h>
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

#define PRIdLEAST8  PRId8
#define PRIdLEAST16 PRId16
#define PRIdLEAST32 PRId32
#define PRIdLEAST64 PRId64
#define PRIiLEAST8  PRIi8
#define PRIiLEAST16 PRIi16
#define PRIiLEAST32 PRIi32
#define PRIiLEAST64 PRIi64
#define PRIoLEAST8  PRIo8
#define PRIoLEAST16 PRIo16
#define PRIoLEAST32 PRIo32
#define PRIoLEAST64 PRIo64
#define PRIuLEAST8  PRIu8
#define PRIuLEAST16 PRIu16
#define PRIuLEAST32 PRIu32
#define PRIuLEAST64 PRIu64
#define PRIxLEAST8  PRIx8
#define PRIxLEAST16 PRIx16
#define PRIxLEAST32 PRIx32
#define PRIxLEAST64 PRIx64
#define PRIXLEAST8  PRIX8
#define PRIXLEAST16 PRIX16
#define PRIXLEAST32 PRIX32
#define PRIXLEAST64 PRIX64

#define PRIdFAST8  PRId8
#define PRIdFAST16 PRId16
#define PRIdFAST32 PRId32
#define PRIdFAST64 PRId64
#define PRIiFAST8  PRIi8
#define PRIiFAST16 PRIi16
#define PRIiFAST32 PRIi32
#define PRIiFAST64 PRIi64
#define PRIoFAST8  PRIo8
#define PRIoFAST16 PRIo16
#define PRIoFAST32 PRIo32
#define PRIoFAST64 PRIo64
#define PRIuFAST8  PRIu8
#define PRIuFAST16 PRIu16
#define PRIuFAST32 PRIu32
#define PRIuFAST64 PRIu64
#define PRIxFAST8  PRIx8
#define PRIxFAST16 PRIx16
#define PRIxFAST32 PRIx32
#define PRIxFAST64 PRIx64
#define PRIXFAST8  PRIX8
#define PRIXFAST16 PRIX16
#define PRIXFAST32 PRIX32
#define PRIXFAST64 PRIX64

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

#define SCNdLEAST8  SCNd8
#define SCNdLEAST16 SCNd16
#define SCNdLEAST32 SCNd32
#define SCNdLEAST64 SCNd64
#define SCNiLEAST8  SCNi8
#define SCNiLEAST16 SCNi16
#define SCNiLEAST32 SCNi32
#define SCNiLEAST64 SCNi64
#define SCNoLEAST8  SCNo8
#define SCNoLEAST16 SCNo16
#define SCNoLEAST32 SCNo32
#define SCNoLEAST64 SCNo64
#define SCNuLEAST8  SCNu8
#define SCNuLEAST16 SCNu16
#define SCNuLEAST32 SCNu32
#define SCNuLEAST64 SCNu64
#define SCNxLEAST8  SCNx8
#define SCNxLEAST16 SCNx16
#define SCNxLEAST32 SCNx32
#define SCNxLEAST64 SCNx64

#define SCNdFAST8  SCNd8
#define SCNdFAST16 SCNd16
#define SCNdFAST32 SCNd32
#define SCNdFAST64 SCNd64
#define SCNiFAST8  SCNi8
#define SCNiFAST16 SCNi16
#define SCNiFAST32 SCNi32
#define SCNiFAST64 SCNi64
#define SCNoFAST8  SCNo8
#define SCNoFAST16 SCNo16
#define SCNoFAST32 SCNo32
#define SCNoFAST64 SCNo64
#define SCNuFAST8  SCNu8
#define SCNuFAST16 SCNu16
#define SCNuFAST32 SCNu32
#define SCNuFAST64 SCNu64
#define SCNxFAST8  SCNx8
#define SCNxFAST16 SCNx16
#define SCNxFAST32 SCNx32
#define SCNxFAST64 SCNx64

intmax_t imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);
intmax_t wcstoimax(const wchar_t *nptr, wchar_t **endptr, int base);
uintmax_t wcstoumax(const wchar_t *nptr, wchar_t **endptr, int base);
#endif
