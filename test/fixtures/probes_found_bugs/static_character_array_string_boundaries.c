#include <assert.h>

static char exact_global[3] = "abc";
static char embedded_null[2] = "a\0";
static char concatenated[3] = "a" "bc";
static signed char signed_text[4] = "abc";
static unsigned char unsigned_text[4] = "abc";
static char inferred_text[] = "abc";
static int wide_text[2] = L"a";

int main(void) {
  static char exact_local[1] = "Z";

  assert(sizeof(exact_global) == 3);
  assert(exact_global[0] == 'a');
  assert(exact_global[1] == 'b');
  assert(exact_global[2] == 'c');
  assert(sizeof(embedded_null) == 2);
  assert(embedded_null[0] == 'a');
  assert(embedded_null[1] == 0);
  assert(sizeof(concatenated) == 3);
  assert(concatenated[2] == 'c');
  assert(signed_text[3] == 0);
  assert(unsigned_text[3] == 0);
  assert(sizeof(inferred_text) == 4);
  assert(inferred_text[3] == 0);
  assert(wide_text[0] == 'a');
  assert(wide_text[1] == 0);
  assert(sizeof(exact_local) == 1);
  assert(exact_local[0] == 'Z');
  return 0;
}
