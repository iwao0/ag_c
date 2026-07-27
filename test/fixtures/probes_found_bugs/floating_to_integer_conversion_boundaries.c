// Floating-to-integer conversion truncates toward zero and uses the target
// integer width and signedness. Keep all values within the representable range,
// then cover explicit/implicit conversions, ABI paths, and single evaluation.
typedef int (*signed_checker)(int, int);
typedef int (*unsigned_checker)(unsigned int, unsigned int);
typedef int (*wide_checker)(long long, long long);

struct integer_box {
  signed char character;
  unsigned short narrow;
  int regular;
  unsigned long long wide;
};

struct signed_bits {
  signed int value : 7;
};

static volatile float negative_float_source = -123.75f;
static volatile float positive_float_source = 250.75f;
static volatile double negative_double_source = -30000.75;
static volatile double positive_double_source = 60000.75;
static volatile double negative_integer_source = -2000000000.75;
static volatile double positive_integer_source = 4000000000.75;
static volatile long double negative_wide_source =
    -4503599627370495.5L;
static volatile long double positive_wide_source =
    9007199254740992.0L;
static int evaluation_count;

static int check_signed(int actual, int expected) {
  return actual == expected;
}

static int check_unsigned(
    unsigned int actual, unsigned int expected) {
  return actual == expected;
}

static int check_wide(long long actual, long long expected) {
  return actual == expected;
}

static double next_negative_double(void) {
  evaluation_count++;
  return -77.875;
}

static signed char return_signed_character(void) {
  return (signed char)negative_float_source;
}

static unsigned char return_unsigned_character(void) {
  return (unsigned char)positive_float_source;
}

static short return_signed_short(void) {
  return (short)negative_double_source;
}

static unsigned short return_unsigned_short(void) {
  return (unsigned short)positive_double_source;
}

static int return_signed_integer(void) {
  return negative_integer_source;
}

static unsigned int return_unsigned_integer(void) {
  return (unsigned int)positive_integer_source;
}

static long long return_signed_wide(void) {
  return (long long)negative_wide_source;
}

static unsigned long long return_unsigned_wide(void) {
  return positive_wide_source;
}

int main(void) {
  signed char signed_character =
      (signed char)negative_float_source;
  unsigned char unsigned_character =
      (unsigned char)positive_float_source;
  short signed_short = (short)negative_double_source;
  unsigned short unsigned_short =
      (unsigned short)positive_double_source;
  int signed_integer = (int)negative_integer_source;
  unsigned int unsigned_integer =
      (unsigned int)positive_integer_source;
  long long signed_wide = (long long)negative_wide_source;
  unsigned long long unsigned_wide =
      (unsigned long long)positive_wide_source;

  if (signed_character != -123 ||
      unsigned_character != 250 ||
      signed_short != -30000 ||
      unsigned_short != 60000 ||
      signed_integer != -2000000000 ||
      unsigned_integer != 4000000000U ||
      signed_wide != -4503599627370495LL ||
      unsigned_wide != 9007199254740992ULL)
    return 1;

  int assigned = 0;
  assigned = negative_float_source;
  if (assigned != -123)
    return 2;
  unsigned int unsigned_assigned = 0;
  unsigned_assigned = positive_double_source;
  if (unsigned_assigned != 60000U)
    return 3;

  struct integer_box box = {
      (signed char)negative_float_source,
      (unsigned short)positive_double_source,
      (int)negative_integer_source,
      (unsigned long long)positive_wide_source,
  };
  if (box.character != -123 ||
      box.narrow != 60000 ||
      box.regular != -2000000000 ||
      box.wide != 9007199254740992ULL)
    return 4;

  signed_checker signed_pointer = check_signed;
  unsigned_checker unsigned_pointer = check_unsigned;
  wide_checker wide_pointer = check_wide;
  if (!check_signed(negative_float_source, -123) ||
      !signed_pointer(negative_double_source, -30000) ||
      !unsigned_pointer(
          (unsigned int)positive_double_source, 60000U) ||
      !wide_pointer(
          (long long)negative_wide_source, -4503599627370495LL))
    return 5;

  if (return_signed_character() != -123 ||
      return_unsigned_character() != 250 ||
      return_signed_short() != -30000 ||
      return_unsigned_short() != 60000 ||
      return_signed_integer() != -2000000000 ||
      return_unsigned_integer() != 4000000000U ||
      return_signed_wide() != -4503599627370495LL ||
      return_unsigned_wide() != 9007199254740992ULL)
    return 6;

  int arithmetic = (int)(negative_double_source + 1.0);
  int conditional =
      1 ? negative_float_source : negative_double_source;
  int comma =
      (evaluation_count++, (int)positive_double_source);
  int compound = 100;
  compound += negative_float_source;
  if (arithmetic != -29999 ||
      conditional != -123 ||
      comma != 60000 ||
      compound != -23 ||
      evaluation_count != 1)
    return 7;

  struct signed_bits bits = {0};
  bits.value = -61.75;
  if (bits.value != -61)
    return 8;

  _Bool truth = (_Bool)0.5;
  _Bool negative_truth = (_Bool)-0.5;
  _Bool zero = (_Bool)-0.0;
  if (truth != 1 || negative_truth != 1 || zero != 0)
    return 9;

  evaluation_count = 0;
  if (!signed_pointer(next_negative_double(), -77) ||
      evaluation_count != 1)
    return 10;

  return 0;
}
