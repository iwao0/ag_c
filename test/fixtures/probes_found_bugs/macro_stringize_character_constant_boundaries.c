/*
 * Stringizing preserves the spelling of character constants.  Backslashes
 * in that spelling must be escaped in the generated string literal so that
 * the resulting runtime string still contains the original backslash.
 */
#include <assert.h>
#include <string.h>

#define STRINGIZE_RAW(value) #value
#define STRINGIZE(value) STRINGIZE_RAW(value)
#define ESCAPED_CHARACTER '\n'

int main(void) {
  assert(strcmp(STRINGIZE_RAW('a'), "'a'") == 0);
  assert(strcmp(STRINGIZE_RAW('\n'), "'\\n'") == 0);
  assert(strcmp(STRINGIZE_RAW('\\'), "'\\\\'") == 0);
  assert(strcmp(STRINGIZE_RAW('"'), "'\"'") == 0);
  assert(strcmp(STRINGIZE_RAW('\''), "'\\''") == 0);
  assert(strcmp(STRINGIZE_RAW('\x41'), "'\\x41'") == 0);
  assert(strcmp(STRINGIZE_RAW(L'\n'), "L'\\n'") == 0);
  assert(strcmp(STRINGIZE_RAW(u'\n'), "u'\\n'") == 0);
  assert(strcmp(STRINGIZE_RAW(U'\n'), "U'\\n'") == 0);
  assert(strcmp(STRINGIZE_RAW(L'\u03A9'), "L'\\u03A9'") == 0);

  assert(strcmp(STRINGIZE_RAW("a\n"), "\"a\\n\"") == 0);
  assert(strcmp(STRINGIZE_RAW(L"wide"), "L\"wide\"") == 0);
  assert(strcmp(STRINGIZE_RAW(u"wide"), "u\"wide\"") == 0);
  assert(strcmp(STRINGIZE_RAW(U"wide"), "U\"wide\"") == 0);
  assert(strcmp(STRINGIZE_RAW(u8"wide"), "u8\"wide\"") == 0);

  assert(strcmp(STRINGIZE_RAW(ESCAPED_CHARACTER),
                "ESCAPED_CHARACTER") == 0);
  assert(strcmp(STRINGIZE(ESCAPED_CHARACTER), "'\\n'") == 0);
  assert(strcmp(STRINGIZE(STRINGIZE_RAW('\n')),
                "\"'\\\\n'\"") == 0);
  return 0;
}
