#include <assert.h>

#
# /* a comment-only null directive */

#define FEATURE_FLAG 1

#if 1
#define SELECTED_VALUE 40
#elif 1 +
#define SELECTED_VALUE 0
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
  assert(SELECTED_VALUE + FEATURE_VALUE + ABSENT_VALUE == 42);
  return 0;
}
