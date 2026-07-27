/*
 * C11 6.10.3.5 #undef boundaries.
 * The directive consumes one unexpanded identifier, may name an undefined
 * ordinary macro, and accepts only whitespace/comments through newline.
 */
#include <assert.h>

#define TARGET_VALUE 42
#define NAME_ALIAS TARGET_VALUE
#undef NAME_ALIAS

#ifdef NAME_ALIAS
#error "#undef must remove the named macro without expanding its name"
#endif

#ifndef TARGET_VALUE
#error "#undef must not remove the macro named by the removed replacement"
#endif

#undef NEVER_DEFINED

#define COMMENT_TERMINATED 1
#undef COMMENT_TERMINATED /* comments are whitespace, not extra tokens */

#ifdef COMMENT_TERMINATED
#error "#undef followed by a comment must still remove the macro"
#endif

/* __LP64__ is implementation-provided rather than a standard immutable macro. */
#undef __LP64__

#ifdef __LP64__
#error "implementation-provided macros remain removable"
#endif

int main(void) {
  assert(TARGET_VALUE == 42);
  return 0;
}
