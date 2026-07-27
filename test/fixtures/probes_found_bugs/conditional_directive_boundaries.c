#include <assert.h>

#
# /* a comment-only null directive */

#define FEATURE_FLAG 1
#define DEFINED_ALIAS MISSING_FLAG

#if !defined FEATURE_FLAG || !defined(FEATURE_FLAG) || \
    !defined(DEFINED_ALIAS) || defined MISSING_FLAG
#error defined must inspect its identifier operand without macro expansion
#endif

#if 'A' != 65 || '\n' != 10 || L'Z' != 90 || u'B' != 66 || U'C' != 67
#error character constants must remain valid in preprocessing expressions
#endif

#if 1
#define SELECTED_VALUE 40
#elif 1 +
#define SELECTED_VALUE 0
#endif

#if 1
#define EMPTY_ELIF_VALUE 1
#elif
#define EMPTY_ELIF_VALUE 0
#endif

#ifdef FEATURE_FLAG /* trailing comments are not extra tokens */
#define FEATURE_VALUE 2
#else /* trailing comments are not extra tokens */
#define FEATURE_VALUE 0
#endif /* trailing comments are not extra tokens */

#ifndef ABSENT_FLAG /* trailing comments are not extra tokens */
#define ABSENT_VALUE 0
#endif /* trailing comments are not extra tokens */

#if 0
#unknown tokens in a skipped preprocessing group
int skipped_tokens @ are_not_c;
#endif

int main(void) {
  assert(SELECTED_VALUE + EMPTY_ELIF_VALUE + FEATURE_VALUE + ABSENT_VALUE == 43);
  return 0;
}
