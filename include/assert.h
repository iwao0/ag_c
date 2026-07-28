#ifndef _ASSERT_H
#define _ASSERT_H

/* C11 7.2.1.1: When the expression is false, write the argument text,
 * __FILE__, __LINE__, and __func__ to standard error in an
 * implementation-defined format, then call abort().  This invokes
 * __assert_rtn from the Apple runtime (the same format and stderr output as
 * clang's <assert.h>).  The previous abort-only implementation emitted no
 * diagnostic and was not C11-conforming. */
_Noreturn void __assert_rtn(
    const char *, const char *, int, const char *);

#define static_assert _Static_assert

#endif

#undef assert
#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
  ((expr) ? (void)0 : __assert_rtn(__func__, __FILE__, __LINE__, #expr))
#endif
