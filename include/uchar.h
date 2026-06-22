#ifndef _UCHAR_H
#define _UCHAR_H
#include <stddef.h>
/* C11 7.28: char16_t / char32_t。Apple ARM64 では uint_least16_t=unsigned short,
 * uint_least32_t=unsigned int。mbstate_t は libc の状態オブジェクト。 */
typedef unsigned short char16_t;
typedef unsigned int   char32_t;
#ifndef __MBSTATE_T_DEFINED
#define __MBSTATE_T_DEFINED
typedef struct { unsigned int __o[32 / sizeof(unsigned int)]; } mbstate_t;
#endif
size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);
#endif
