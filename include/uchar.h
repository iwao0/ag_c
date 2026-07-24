#ifndef _UCHAR_H
#define _UCHAR_H
#include <stddef.h>
/* C11 7.28: char16_t / char32_t。Apple ARM64 では uint_least16_t=unsigned short,
 * uint_least32_t=unsigned int。 */
typedef unsigned short char16_t;
typedef unsigned int   char32_t;
#ifndef __MBSTATE_T_DEFINED
#define __MBSTATE_T_DEFINED
#ifdef __wasm32__
typedef struct { unsigned int __o[32 / sizeof(unsigned int)]; } mbstate_t;
#else
/*
 * Match Darwin's 128-byte, 8-byte-aligned opaque state while retaining a
 * private word view for the native uchar fallback below.
 */
typedef union {
  char __mbstate8[128];
  long long _mbstateL;
  unsigned int __o[128 / sizeof(unsigned int)];
} mbstate_t;
#endif
#endif

#ifdef __wasm32__
size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);
#else

/*
 * Darwin libc does not export the C11 uchar conversion entry points.  Keep a
 * UTF-8 implementation in the bundled header for native output while Wasm
 * continues to use its runtime symbols.  The first five mbstate_t words match
 * the Wasm runtime state representation.
 */
#include <errno.h>

static inline void __ag_uchar_state_reset(mbstate_t *state) {
  state->__o[0] = 0;
  state->__o[1] = 0;
  state->__o[2] = 0;
  state->__o[3] = 0;
  state->__o[4] = 0;
}

static inline int __ag_uchar_utf8_need(unsigned int byte) {
  if (byte < 0x80) return 1;
  if (byte >= 0xc2 && byte <= 0xdf) return 2;
  if (byte >= 0xe0 && byte <= 0xef) return 3;
  if (byte >= 0xf0 && byte <= 0xf4) return 4;
  return 0;
}

static inline int __ag_uchar_utf8_value(
    const mbstate_t *state, unsigned int *value_out) {
  unsigned int bytes = state->__o[0];
  unsigned int need = state->__o[2];
  unsigned int b0 = bytes & 0xff;
  unsigned int b1 = (bytes >> 8) & 0xff;
  unsigned int b2 = (bytes >> 16) & 0xff;
  unsigned int b3 = (bytes >> 24) & 0xff;
  unsigned int value;

  if (need == 1) {
    value = b0;
  } else if (need == 2) {
    value = ((b0 & 0x1f) << 6) | (b1 & 0x3f);
  } else if (need == 3) {
    value = ((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
  } else {
    value = ((b0 & 0x07) << 18) | ((b1 & 0x3f) << 12) |
            ((b2 & 0x3f) << 6) | (b3 & 0x3f);
  }
  if ((need == 2 && value < 0x80) ||
      (need == 3 && value < 0x800) ||
      (need == 4 && value < 0x10000) ||
      (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) {
    return 0;
  }
  *value_out = value;
  return 1;
}

static inline size_t __ag_uchar_decode(
    char32_t *out, const char *s, size_t n, mbstate_t *state) {
  size_t consumed = 0;
  unsigned int value = 0;

  if (!s) {
    __ag_uchar_state_reset(state);
    return 0;
  }
  if (state->__o[4] != 0) {
    __ag_uchar_state_reset(state);
    errno = EILSEQ;
    return (size_t)-1;
  }
  if (state->__o[1] == 0) {
    int need;
    if (n == 0) return (size_t)-2;
    need = __ag_uchar_utf8_need((unsigned char)s[0]);
    if (!need) {
      errno = EILSEQ;
      return (size_t)-1;
    }
    state->__o[2] = (unsigned int)need;
  }
  while (state->__o[1] < state->__o[2] && consumed < n) {
    unsigned int byte = (unsigned char)s[consumed];
    if (state->__o[1] > 0 && (byte & 0xc0) != 0x80) {
      __ag_uchar_state_reset(state);
      errno = EILSEQ;
      return (size_t)-1;
    }
    state->__o[0] |= byte << (state->__o[1] * 8);
    state->__o[1]++;
    consumed++;
  }
  if (state->__o[1] < state->__o[2]) return (size_t)-2;
  if (!__ag_uchar_utf8_value(state, &value)) {
    __ag_uchar_state_reset(state);
    errno = EILSEQ;
    return (size_t)-1;
  }
  __ag_uchar_state_reset(state);
  if (out) *out = (char32_t)value;
  return value == 0 ? 0 : consumed;
}

static inline size_t __ag_uchar_encode(char *s, char32_t value) {
  if (value >= 0xd800 && value <= 0xdfff) {
    errno = EILSEQ;
    return (size_t)-1;
  }
  if (value > 0x10ffff) {
    errno = EILSEQ;
    return (size_t)-1;
  }
  if (value <= 0x7f) {
    s[0] = (char)value;
    return 1;
  }
  if (value <= 0x7ff) {
    s[0] = (char)(0xc0 | ((value >> 6) & 0x1f));
    s[1] = (char)(0x80 | (value & 0x3f));
    return 2;
  }
  if (value <= 0xffff) {
    s[0] = (char)(0xe0 | ((value >> 12) & 0x0f));
    s[1] = (char)(0x80 | ((value >> 6) & 0x3f));
    s[2] = (char)(0x80 | (value & 0x3f));
    return 3;
  }
  s[0] = (char)(0xf0 | ((value >> 18) & 0x07));
  s[1] = (char)(0x80 | ((value >> 12) & 0x3f));
  s[2] = (char)(0x80 | ((value >> 6) & 0x3f));
  s[3] = (char)(0x80 | (value & 0x3f));
  return 4;
}

static inline size_t mbrtoc16(
    char16_t *pc16, const char *s, size_t n, mbstate_t *ps) {
  static mbstate_t fallback = {{0}};
  mbstate_t *state = ps ? ps : &fallback;
  char32_t value = 0;
  size_t result;

  if (!s) {
    __ag_uchar_state_reset(state);
    return 0;
  }
  if (state->__o[4] == 1) {
    if (pc16) *pc16 = (char16_t)state->__o[3];
    __ag_uchar_state_reset(state);
    return (size_t)-3;
  }
  result = __ag_uchar_decode(&value, s, n, state);
  if (result == (size_t)-1 || result == (size_t)-2) return result;
  if (value > 0xffff) {
    unsigned int scalar = (unsigned int)value - 0x10000;
    if (pc16) *pc16 = (char16_t)(0xd800 + (scalar >> 10));
    state->__o[3] = 0xdc00 + (scalar & 0x3ff);
    state->__o[4] = 1;
  } else if (pc16) {
    *pc16 = (char16_t)value;
  }
  return result;
}

static inline size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps) {
  static mbstate_t fallback = {{0}};
  mbstate_t *state = ps ? ps : &fallback;

  if (!s) {
    __ag_uchar_state_reset(state);
    return 1;
  }
  if (state->__o[4] == 2) {
    unsigned int scalar;
    if (c16 < 0xdc00 || c16 > 0xdfff) {
      __ag_uchar_state_reset(state);
      errno = EILSEQ;
      return (size_t)-1;
    }
    scalar = 0x10000 + ((state->__o[3] - 0xd800) << 10) +
             ((unsigned int)c16 - 0xdc00);
    __ag_uchar_state_reset(state);
    return __ag_uchar_encode(s, (char32_t)scalar);
  }
  if (c16 >= 0xd800 && c16 <= 0xdbff) {
    state->__o[3] = (unsigned int)c16;
    state->__o[4] = 2;
    return 0;
  }
  if (c16 >= 0xdc00 && c16 <= 0xdfff) {
    errno = EILSEQ;
    return (size_t)-1;
  }
  return __ag_uchar_encode(s, (char32_t)c16);
}

static inline size_t mbrtoc32(
    char32_t *pc32, const char *s, size_t n, mbstate_t *ps) {
  static mbstate_t fallback = {{0}};
  mbstate_t *state = ps ? ps : &fallback;
  return __ag_uchar_decode(pc32, s, n, state);
}

static inline size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps) {
  static mbstate_t fallback = {{0}};
  mbstate_t *state = ps ? ps : &fallback;
  if (!s) {
    __ag_uchar_state_reset(state);
    return 1;
  }
  if (state->__o[1] || state->__o[2] || state->__o[4]) {
    __ag_uchar_state_reset(state);
    errno = EILSEQ;
    return (size_t)-1;
  }
  return __ag_uchar_encode(s, c32);
}

#endif
#endif
