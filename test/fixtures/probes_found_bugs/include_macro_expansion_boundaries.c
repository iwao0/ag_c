// C11 6.10.2p4: #include が直接の "..." / <...> ではなくpp-token形式なら、
// macro replacement後の結果をheader形式として再解釈する。
// 修正前は先頭TK_IDENTを空pathとして扱いE1001。
#define OBJECT_HEADER \
  "test/fixtures/probes_found_bugs/include_macro_object.h"
#define OBJECT_HEADER_ALIAS OBJECT_HEADER
#include OBJECT_HEADER_ALIAS
#include /* leading comment */ \
    "test/fixtures/probes_found_bugs/include_macro_object.h" /* trailing comment */

#define STRINGIZE_IMPL(tokens) #tokens
#define STRINGIZE(tokens) STRINGIZE_IMPL(tokens)
#define HEADER_FROM_TOKENS(tokens) STRINGIZE(tokens)
#include HEADER_FROM_TOKENS( \
    test/fixtures/probes_found_bugs/include_macro_function.h)

#define STANDARD_HEADER <stddef.h>
#define STANDARD_HEADER_ALIAS STANDARD_HEADER
#include STANDARD_HEADER_ALIAS
#include /* leading comment */ <stddef.h> /* trailing comment */

_Static_assert(INCLUDE_MACRO_OBJECT_VALUE == 17, "object macro include");
_Static_assert(offsetof(struct include_macro_object_payload, value) == 4,
               "angle macro include");

int main(void) {
  struct include_macro_object_payload payload = {
      'm', INCLUDE_MACRO_OBJECT_VALUE};
  if (payload.lead != 'm' || payload.value != 17) return 1;
  if (include_macro_function_value() != 19) return 2;
  if ((size_t)payload.value != (size_t)17) return 3;
  return 0;
}
