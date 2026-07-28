/*
 * C11 6.7.9p14-15 ties each string-literal prefix to a compatible array
 * element type.  Equal storage width alone is insufficient.  Exact-size
 * arrays may omit only the implicit terminating null code unit.
 */
#include <assert.h>

static const signed char ordinary_exact[2] = "hi";
static volatile unsigned char utf8_exact[2] = u8"hi";
static const unsigned short utf16_exact[2] = u"hi";
static const unsigned int utf32_exact[2] = U"hi";
static const int wide_exact[2] = L"hi";

struct encoded_fields {
  unsigned short utf16[3];
  unsigned int utf32[3];
  int wide[3];
  char utf8[3];
};

static const struct encoded_fields global_fields = {
    u"hi", U"hi", L"hi", u8"hi"};

static int check_local_fields(void) {
  const struct encoded_fields local_fields = {
      u"ok", U"ok", L"ok", u8"ok"};
  return local_fields.utf16[0] == 'o' &&
         local_fields.utf16[2] == 0 &&
         local_fields.utf32[1] == 'k' &&
         local_fields.utf32[2] == 0 &&
         local_fields.wide[0] == 'o' &&
         local_fields.wide[2] == 0 &&
         local_fields.utf8[1] == 'k' &&
         local_fields.utf8[2] == 0;
}

int main(void) {
  assert(sizeof ordinary_exact == 2);
  assert(ordinary_exact[0] == 'h' && ordinary_exact[1] == 'i');
  assert(sizeof utf8_exact == 2);
  assert(utf8_exact[0] == 'h' && utf8_exact[1] == 'i');
  assert(sizeof utf16_exact == 2 * sizeof(unsigned short));
  assert(utf16_exact[0] == 'h' && utf16_exact[1] == 'i');
  assert(sizeof utf32_exact == 2 * sizeof(unsigned int));
  assert(utf32_exact[0] == 'h' && utf32_exact[1] == 'i');
  assert(sizeof wide_exact == 2 * sizeof(int));
  assert(wide_exact[0] == 'h' && wide_exact[1] == 'i');
  assert(global_fields.utf16[2] == 0);
  assert(global_fields.utf32[2] == 0);
  assert(global_fields.wide[2] == 0);
  assert(global_fields.utf8[2] == 0);
  assert(check_local_fields());
  return 0;
}
