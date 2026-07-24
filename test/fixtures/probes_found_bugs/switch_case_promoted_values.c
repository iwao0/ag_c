#include <assert.h>
#include <limits.h>

static int select_unsigned(unsigned value) {
  switch (value) {
    case UINT_MAX:
      return 42;
    case 0:
      return 0;
    default:
      return 1;
  }
}

static int select_signed(int value) {
  switch (value) {
    case -1:
      return 42;
    case 0:
      return 0;
    default:
      return 1;
  }
}

static int select_unsigned_with_negative_label(unsigned value) {
  switch (value) {
    case -1:
      return 42;
    default:
      return 0;
  }
}

static int select_signed_with_unsigned_label(int value) {
  switch (value) {
    case 4294967295u:
      return 42;
    default:
      return 0;
  }
}

static int select_promoted_unsigned_char(unsigned char value) {
  switch (value) {
    case -1:
      return 1;
    case 255:
      return 42;
    default:
      return 0;
  }
}

static int select_promoted_unsigned_short(unsigned short value) {
  switch (value) {
    case -1:
      return 1;
    case 65535:
      return 42;
    default:
      return 0;
  }
}

int main(void) {
  assert(select_unsigned(UINT_MAX) == 42);
  assert(select_signed(-1) == 42);
  assert(select_unsigned_with_negative_label(UINT_MAX) == 42);
  assert(select_signed_with_unsigned_label(-1) == 42);
  assert(select_promoted_unsigned_char(255) == 42);
  assert(select_promoted_unsigned_short(65535) == 42);
  return 0;
}
