/*
 * Token pasting can form an encoding-prefixed string or character literal.
 * The complete spelling of the literal operand, including quotes and escape
 * sequences, must participate in the paste before the result is retokenized.
 */
#include <assert.h>
#include <string.h>

#define PASTE_RAW(left, right) left ## right
#define PASTE(left, right) PASTE_RAW(left, right)
#define STRINGIZE_RAW(value) #value
#define STRINGIZE(value) STRINGIZE_RAW(value)
#define WIDE_PREFIX L

static const char utf8_text[] = PASTE_RAW(u8, "\u03A9");
static const unsigned short utf16_text[] = PASTE_RAW(u, "\u03A9");
static const unsigned int utf32_text[] = PASTE_RAW(U, "\U0001F600");
static const int wide_text[] = PASTE(WIDE_PREFIX, "\u03A9");

int main(void) {
  assert(sizeof(utf8_text) == 3);
  assert((unsigned char)utf8_text[0] == 0xCE);
  assert((unsigned char)utf8_text[1] == 0xA9);
  assert(utf8_text[2] == 0);

  assert(sizeof(utf16_text) == 2 * sizeof(unsigned short));
  assert(utf16_text[0] == 0x03A9);
  assert(utf16_text[1] == 0);

  assert(sizeof(utf32_text) == 2 * sizeof(unsigned int));
  assert(utf32_text[0] == 0x1F600);
  assert(utf32_text[1] == 0);

  assert(sizeof(wide_text) == 2 * sizeof(int));
  assert(wide_text[0] == 0x03A9);
  assert(wide_text[1] == 0);

  assert(PASTE_RAW(L, '\u03A9') == 0x03A9);
  assert(PASTE_RAW(u, '\u03A9') == 0x03A9);
  assert(PASTE_RAW(U, '\U0001F600') == 0x1F600);
  assert(strcmp(STRINGIZE(PASTE_RAW(L, '\n')), "L'\\n'") == 0);
  assert(strcmp(STRINGIZE(PASTE_RAW(L, "\n")), "L\"\\n\"") == 0);
  return 0;
}
