/*
 * __func__ in a macro is interpreted in the function where the macro is
 * expanded.  UCN and raw UTF-8 spellings of function identifiers must produce
 * the same normalized UTF-8 function-name contents.
 */
#include <assert.h>
#include <string.h>

#define CURRENT_FUNCTION_NAME() __func__
#define CURRENT_FUNCTION_OBJECT() (&__func__)

#ifdef __func__
#error "__func__ is a predefined identifier, not a macro"
#endif

static const char *caf\u00E9(void) {
  static const char *name = CURRENT_FUNCTION_NAME();
  _Static_assert(
      sizeof(CURRENT_FUNCTION_NAME()) == sizeof("caf\u00E9"),
      "UCN function name byte length");
  assert(name == CURRENT_FUNCTION_NAME());
  assert(CURRENT_FUNCTION_OBJECT() == &__func__);
  return name;
}

static const char *\u03A9(void) {
  static const char *name = CURRENT_FUNCTION_NAME();
  _Static_assert(
      sizeof(CURRENT_FUNCTION_NAME()) == sizeof("\u03A9"),
      "UCN function name byte length");
  assert(name == CURRENT_FUNCTION_NAME());
  return name;
}

static const char *日本語(void) {
  static const char *name = CURRENT_FUNCTION_NAME();
  _Static_assert(
      sizeof(CURRENT_FUNCTION_NAME()) == sizeof("日本語"),
      "raw UTF-8 function name byte length");
  assert(name == CURRENT_FUNCTION_NAME());
  return name;
}

int main(void) {
  assert(strcmp(café(), "café") == 0);
  assert(strcmp(\u03A9(), "\u03A9") == 0);
  assert(strcmp(日本語(), "日本語") == 0);
  assert(café() == café());
  assert(Ω() == Ω());
  assert(日本語() == 日本語());
  assert(strcmp(CURRENT_FUNCTION_NAME(), "main") == 0);
  return 0;
}
