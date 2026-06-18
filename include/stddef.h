#ifndef _STDDEF_H
#define _STDDEF_H

typedef unsigned long size_t;
typedef long ptrdiff_t;
/* C11 7.19: stddef.h は wchar_t / max_align_t も定義する。
 * このターゲット (Apple ARM64) では clang と同じく wchar_t=int (4B, signed,
 * __WCHAR_TYPE__=int)、max_align_t=long double (size/align=8)。 */
typedef int wchar_t;
typedef long double max_align_t;

#define NULL ((void *)0)
#define offsetof(type, member) ((size_t)&((type *)0)->member)

#endif
