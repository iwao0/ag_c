/*
 * UCN escapes in ordinary and u8 string literals are encoded as UTF-8 bytes.
 * Raw UTF-8 and hexadecimal byte escapes keep their existing byte spelling.
 */
#include <assert.h>

static const char global_ordinary_ucn[] = "\u03A9";
static const char global_u8_ucn[] = u8"\u03A9";
static const char global_supplementary_ucn[] = u8"\U0001F600";
static const char global_raw_utf8[] = "Ω";
static const char global_hex_bytes[] = "\xCE\xA9";

struct NarrowStrings {
  char omega[3];
  char supplementary[5];
};

static const struct NarrowStrings global_struct = {
    "\u03A9", u8"\U0001F600"};
static const char global_rows[2][5] = {
    "\u03A9", u8"\U0001F600"};

_Static_assert(sizeof("\u03A9") == 3, "ordinary UCN uses two UTF-8 bytes");
_Static_assert(sizeof(u8"\u03A9") == 3, "u8 UCN uses two UTF-8 bytes");
_Static_assert(sizeof(u8"\U0001F600") == 5,
               "supplementary UCN uses four UTF-8 bytes");
_Static_assert(sizeof("\xCE\xA9") == 3,
               "hex escapes remain individual bytes");

static void assert_omega(const char text[3]) {
  assert((unsigned char)text[0] == 0xCE);
  assert((unsigned char)text[1] == 0xA9);
  assert(text[2] == 0);
}

int main(void) {
  const char local_ordinary_ucn[] = "\u03A9";
  const char local_u8_ucn[] = u8"\u03A9";
  const char local_raw_utf8[] = "Ω";
  const char local_hex_bytes[] = "\xCE\xA9";
  const char *supplementary = u8"\U0001F600";
  const struct NarrowStrings local_struct = {
      .omega = u8"\u03A9",
      .supplementary = "\U0001F600",
  };
  const char local_rows[2][5] = {
      u8"\u03A9", "\U0001F600"};

  assert(sizeof(global_ordinary_ucn) == 3);
  assert(sizeof(global_u8_ucn) == 3);
  assert(sizeof(global_supplementary_ucn) == 5);
  assert(sizeof(global_raw_utf8) == 3);
  assert(sizeof(global_hex_bytes) == 3);
  assert(sizeof(local_ordinary_ucn) == 3);
  assert(sizeof(local_u8_ucn) == 3);
  assert(sizeof(local_raw_utf8) == 3);
  assert(sizeof(local_hex_bytes) == 3);

  assert_omega(global_ordinary_ucn);
  assert_omega(global_u8_ucn);
  assert_omega(global_raw_utf8);
  assert_omega(global_hex_bytes);
  assert_omega(local_ordinary_ucn);
  assert_omega(local_u8_ucn);
  assert_omega(local_raw_utf8);
  assert_omega(local_hex_bytes);
  assert_omega(global_struct.omega);
  assert_omega(local_struct.omega);
  assert_omega(global_rows[0]);
  assert_omega(local_rows[0]);

  assert((unsigned char)global_supplementary_ucn[0] == 0xF0);
  assert((unsigned char)global_supplementary_ucn[1] == 0x9F);
  assert((unsigned char)global_supplementary_ucn[2] == 0x98);
  assert((unsigned char)global_supplementary_ucn[3] == 0x80);
  assert(global_supplementary_ucn[4] == 0);
  assert((unsigned char)supplementary[0] == 0xF0);
  assert((unsigned char)supplementary[3] == 0x80);
  assert(supplementary[4] == 0);
  assert((unsigned char)global_struct.supplementary[0] == 0xF0);
  assert((unsigned char)global_struct.supplementary[3] == 0x80);
  assert(global_struct.supplementary[4] == 0);
  assert((unsigned char)local_struct.supplementary[0] == 0xF0);
  assert((unsigned char)local_struct.supplementary[3] == 0x80);
  assert(local_struct.supplementary[4] == 0);
  assert((unsigned char)global_rows[1][0] == 0xF0);
  assert((unsigned char)global_rows[1][3] == 0x80);
  assert(global_rows[1][4] == 0);
  assert((unsigned char)local_rows[1][0] == 0xF0);
  assert((unsigned char)local_rows[1][3] == 0x80);
  assert(local_rows[1][4] == 0);
  return 0;
}
