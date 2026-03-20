/* stdarg.h - ag_c simplified variadic argument support (Apple ARM64)
 *
 * Calling convention (ag_c-specific):
 *   Named params and the first (8 - nnamed) variadic args are passed in
 *   x0..x7.  In a variadic funcdef prologue the compiler saves all of
 *   x0..x7 to consecutive 8-byte slots starting at [x29, #24]:
 *     param[i] -> [x29, #16 + (i+1)*8]  (i = 0..nnamed-1)
 *     vararg[j] -> [x29, #16 + (nnamed+j+1)*8]  (j = 0..7-nnamed)
 *
 * va_start(ap, last_named):
 *   ap = &last_named + 8  (next 8-byte slot after the last named param)
 *
 * va_arg(ap, type):
 *   advances ap by 8 bytes, returns *(type *)(old_ap)
 *
 * Supports up to 8 total arguments (named + variadic).
 */

#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

/* Set ap to point at the first variadic argument.
 * Uses integer arithmetic (via long) to avoid pointer-size ambiguity. */
#define va_start(ap, last) ((ap) = (va_list)((long)&(last) + 8))

/* Fetch the next variadic argument of the given type and advance ap. */
#define va_arg(ap, type) (*(type *)((long)(ap += 8) - 8))

/* No-op cleanup. */
#define va_end(ap) ((void)(ap))

#endif /* _STDARG_H */
