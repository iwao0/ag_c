/*
 * The integer-valued predefined macros are pp-number tokens with value and
 * type int.  Preserve their exact spelling through stringization and token
 * pasting instead of treating them as value-only semantic constants.
 */
#include <assert.h>
#include <string.h>

#define STRINGIZE_RAW(value) #value
#define STRINGIZE(value) STRINGIZE_RAW(value)
#define PASTE_RAW(left, right) left ## right
#define PASTE(left, right) PASTE_RAW(left, right)

#define ASSERT_PREDEFINED_INT_ONE(name)                                  \
  _Static_assert((name) == 1, #name " value");                          \
  _Static_assert(_Generic((name), int: 1, default: 0), #name " type");  \
  _Static_assert(sizeof(STRINGIZE(name)) == sizeof("1"),                 \
                 #name " spelling");                                    \
  _Static_assert(_Generic(PASTE(name, U), unsigned int: 1, default: 0),  \
                 #name " token paste")

ASSERT_PREDEFINED_INT_ONE(__STDC__);
ASSERT_PREDEFINED_INT_ONE(__STDC_HOSTED__);
ASSERT_PREDEFINED_INT_ONE(__STDC_NO_THREADS__);
ASSERT_PREDEFINED_INT_ONE(__STDC_UTF_16__);
ASSERT_PREDEFINED_INT_ONE(__STDC_UTF_32__);

#ifdef __LP64__
ASSERT_PREDEFINED_INT_ONE(__LP64__);
#endif

#ifdef __wasm32__
ASSERT_PREDEFINED_INT_ONE(__wasm32__);
#endif

int main(void) {
  assert(strcmp(STRINGIZE(__STDC__), "1") == 0);
  assert(strcmp(STRINGIZE(__STDC_HOSTED__), "1") == 0);
  assert(strcmp(STRINGIZE(__STDC_NO_THREADS__), "1") == 0);
  assert(strcmp(STRINGIZE(__STDC_UTF_16__), "1") == 0);
  assert(strcmp(STRINGIZE(__STDC_UTF_32__), "1") == 0);
#ifdef __LP64__
  assert(strcmp(STRINGIZE(__LP64__), "1") == 0);
#endif
#ifdef __wasm32__
  assert(strcmp(STRINGIZE(__wasm32__), "1") == 0);
#endif
  return 0;
}
