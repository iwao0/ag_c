/*
 * C11 requires printf/scanf format macros for every least- and fast-width
 * integer type exposed by <stdint.h>.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static const char *const print_formats[] = {
    PRIdLEAST8, PRIdLEAST16, PRIdLEAST32, PRIdLEAST64,
    PRIiLEAST8, PRIiLEAST16, PRIiLEAST32, PRIiLEAST64,
    PRIoLEAST8, PRIoLEAST16, PRIoLEAST32, PRIoLEAST64,
    PRIuLEAST8, PRIuLEAST16, PRIuLEAST32, PRIuLEAST64,
    PRIxLEAST8, PRIxLEAST16, PRIxLEAST32, PRIxLEAST64,
    PRIXLEAST8, PRIXLEAST16, PRIXLEAST32, PRIXLEAST64,
    PRIdFAST8, PRIdFAST16, PRIdFAST32, PRIdFAST64,
    PRIiFAST8, PRIiFAST16, PRIiFAST32, PRIiFAST64,
    PRIoFAST8, PRIoFAST16, PRIoFAST32, PRIoFAST64,
    PRIuFAST8, PRIuFAST16, PRIuFAST32, PRIuFAST64,
    PRIxFAST8, PRIxFAST16, PRIxFAST32, PRIxFAST64,
    PRIXFAST8, PRIXFAST16, PRIXFAST32, PRIXFAST64,
};

static const char *const expected_print_formats[] = {
    "hhd", "hd", "d", "lld",
    "hhi", "hi", "i", "lli",
    "hho", "ho", "o", "llo",
    "hhu", "hu", "u", "llu",
    "hhx", "hx", "x", "llx",
    "hhX", "hX", "X", "llX",
    "hhd", "hd", "d", "lld",
    "hhi", "hi", "i", "lli",
    "hho", "ho", "o", "llo",
    "hhu", "hu", "u", "llu",
    "hhx", "hx", "x", "llx",
    "hhX", "hX", "X", "llX",
};

static const char *const scan_formats[] = {
    SCNdLEAST8, SCNdLEAST16, SCNdLEAST32, SCNdLEAST64,
    SCNiLEAST8, SCNiLEAST16, SCNiLEAST32, SCNiLEAST64,
    SCNoLEAST8, SCNoLEAST16, SCNoLEAST32, SCNoLEAST64,
    SCNuLEAST8, SCNuLEAST16, SCNuLEAST32, SCNuLEAST64,
    SCNxLEAST8, SCNxLEAST16, SCNxLEAST32, SCNxLEAST64,
    SCNdFAST8, SCNdFAST16, SCNdFAST32, SCNdFAST64,
    SCNiFAST8, SCNiFAST16, SCNiFAST32, SCNiFAST64,
    SCNoFAST8, SCNoFAST16, SCNoFAST32, SCNoFAST64,
    SCNuFAST8, SCNuFAST16, SCNuFAST32, SCNuFAST64,
    SCNxFAST8, SCNxFAST16, SCNxFAST32, SCNxFAST64,
};

static const char *const expected_scan_formats[] = {
    "hhd", "hd", "d", "lld",
    "hhi", "hi", "i", "lli",
    "hho", "ho", "o", "llo",
    "hhu", "hu", "u", "llu",
    "hhx", "hx", "x", "llx",
    "hhd", "hd", "d", "lld",
    "hhi", "hi", "i", "lli",
    "hho", "ho", "o", "llo",
    "hhu", "hu", "u", "llu",
    "hhx", "hx", "x", "llx",
};

static int verify_format_tables(void) {
  unsigned long index;

  if (ARRAY_COUNT(print_formats) != ARRAY_COUNT(expected_print_formats) ||
      ARRAY_COUNT(scan_formats) != ARRAY_COUNT(expected_scan_formats)) {
    return 0;
  }
  for (index = 0; index < ARRAY_COUNT(print_formats); ++index) {
    if (strcmp(print_formats[index], expected_print_formats[index]) != 0) {
      return 0;
    }
  }
  for (index = 0; index < ARRAY_COUNT(scan_formats); ++index) {
    if (strcmp(scan_formats[index], expected_scan_formats[index]) != 0) {
      return 0;
    }
  }
  return 1;
}

int main(void) {
  char buffer[160];
  int_least8_t least8 = -12;
  int_least16_t least16 = 1234;
  uint_least32_t least32 = 01234567U;
  uint_least64_t least64 = 0x123456789abcdef0ULL;
  int_fast8_t fast8 = 0x5a;
  int_fast16_t fast16 = 0x1234;
  int_fast32_t fast32 = -7654321;
  uint_fast64_t fast64 = 18446744073709551610ULL;
  int_least8_t scanned_least8 = 0;
  int_least16_t scanned_least16 = 0;
  uint_least32_t scanned_least32 = 0;
  uint_least64_t scanned_least64 = 0;
  int_fast8_t scanned_fast8 = 0;
  int_fast16_t scanned_fast16 = 0;
  int_fast32_t scanned_fast32 = 0;
  uint_fast64_t scanned_fast64 = 0;

  if (!verify_format_tables()) return 1;
  if (snprintf(buffer, sizeof(buffer),
               "%" PRIdLEAST8 " %" PRIiLEAST16 " %" PRIoLEAST32
               " %" PRIxLEAST64 " %" PRIxFAST8 " %" PRIXFAST16
               " %" PRIdFAST32 " %" PRIuFAST64,
               least8, least16, least32, least64,
               fast8, fast16, fast32, fast64) <= 0) {
    return 2;
  }
  if (strcmp(buffer,
             "-12 1234 1234567 123456789abcdef0 5a 1234 -7654321 "
             "18446744073709551610") != 0) {
    return 3;
  }
  if (sscanf(buffer,
             "%" SCNdLEAST8 " %" SCNiLEAST16 " %" SCNoLEAST32
             " %" SCNxLEAST64 " %" SCNxFAST8 " %" SCNxFAST16
             " %" SCNdFAST32 " %" SCNuFAST64,
             &scanned_least8, &scanned_least16, &scanned_least32,
             &scanned_least64, &scanned_fast8, &scanned_fast16,
             &scanned_fast32, &scanned_fast64) != 8) {
    return 4;
  }
  if (scanned_least8 != least8) return 5;
  if (scanned_least16 != least16) return 6;
  if (scanned_least32 != least32) return 7;
  if (scanned_least64 != least64) return 8;
  if (scanned_fast8 != fast8) return 9;
  if (scanned_fast16 != fast16) return 10;
  if (scanned_fast32 != fast32) return 11;
  if (scanned_fast64 != fast64) return 12;
  return 0;
}
