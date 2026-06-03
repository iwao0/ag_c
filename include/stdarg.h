/* stdarg.h - ag_c variadic argument support (Apple ARM64 ABI)
 *
 * Apple ARM64 calling convention for variadic functions:
 *   - Named parameters are passed in x0..x7 / d0..d7 per ARM64 ABI.
 *   - All variadic arguments are passed on the STACK (each occupies an
 *     8-byte slot), starting at the caller's stack pointer at call time.
 *   - From the callee's perspective, this is the address x29 + STACK_SIZE
 *     after the standard prologue. The compiler-provided identifier
 *     `__va_arg_area` resolves to that address at runtime.
 *
 * va_list:
 *   Single pointer (char*) walking the contiguous stack array of
 *   variadic arguments.
 *
 * va_start(ap, last_named):
 *   Sets ap to the start of the variadic stack area. `last_named` is
 *   unused with this convention; we accept it for source compatibility.
 *
 * va_arg(ap, type):
 *   Reads *(type *)ap, then advances ap by 8 bytes (one slot).
 */

#ifndef _STDARG_H
#define _STDARG_H

typedef char *va_list;

#define va_start(ap, last) ((void)(last), (ap) = (va_list)__va_arg_area)

#define va_arg(ap, type) (*(type *)((long)(ap += 8) - 8))

#define va_end(ap) ((void)(ap))

/* Copy current state of src ap into dest ap. C11 7.16.1.2. */
#define va_copy(dest, src) ((dest) = (src))

#endif /* _STDARG_H */
