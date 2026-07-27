// Integer-to-floating conversion must preserve signedness and source width.
// Cover every integer width, runtime narrowing casts, aggregate initialization,
// arithmetic, returns, direct/indirect calls, bit-fields, and single evaluation.
typedef int (*double_checker)(double, double);

struct floating_box {
  float narrow;
  double wide;
};

struct signed_bits {
  signed int value : 7;
};

enum signed_value {
  ENUM_NEGATIVE = -91
};

static volatile signed char signed_character_source = -123;
static volatile unsigned char unsigned_character_source = 250;
static volatile short signed_short_source = -30000;
static volatile unsigned short unsigned_short_source = 60000;
static volatile int signed_integer_source = -2000000000;
static volatile unsigned int unsigned_integer_source = 4000000000U;
static volatile long long signed_wide_source = -9007199254740992LL;
static volatile unsigned long long unsigned_wide_source =
    9007199254740992ULL;
static volatile int signed_character_cast_source = 200;
static volatile int signed_short_cast_source = 40000;
static int evaluation_count;

static int close_double(double actual, double expected) {
  return actual == expected;
}

static int close_float(float actual, float expected) {
  return actual == expected;
}

static int next_negative_integer(void) {
  evaluation_count++;
  return -77;
}

static double return_signed_character(void) {
  return signed_character_source;
}

static double return_signed_short(void) {
  return signed_short_source;
}

static double return_signed_integer(void) {
  return signed_integer_source;
}

static double return_unsigned_integer(void) {
  return unsigned_integer_source;
}

static double return_signed_wide(void) {
  return signed_wide_source;
}

static double return_unsigned_wide(void) {
  return unsigned_wide_source;
}

int main(void) {
  signed char signed_character = signed_character_source;
  unsigned char unsigned_character = unsigned_character_source;
  short signed_short = signed_short_source;
  unsigned short unsigned_short = unsigned_short_source;
  int signed_integer = signed_integer_source;
  unsigned int unsigned_integer = unsigned_integer_source;
  long long signed_wide = signed_wide_source;
  unsigned long long unsigned_wide = unsigned_wide_source;

  float narrow_signed_character = signed_character;
  float narrow_unsigned_character = unsigned_character;
  float narrow_signed_short = signed_short;
  float narrow_unsigned_short = unsigned_short;
  if (!close_float(narrow_signed_character, -123.0f) ||
      !close_float(narrow_unsigned_character, 250.0f) ||
      !close_float(narrow_signed_short, -30000.0f) ||
      !close_float(narrow_unsigned_short, 60000.0f))
    return 1;
  if (!close_float(
          (signed char)signed_character_cast_source, -56.0f) ||
      !close_double(
          (short)signed_short_cast_source, -25536.0))
    return 2;

  double converted[] = {
      signed_character,
      unsigned_character,
      signed_short,
      unsigned_short,
      signed_integer,
      unsigned_integer,
      signed_wide,
      unsigned_wide,
  };
  double expected[] = {
      -123.0,
      250.0,
      -30000.0,
      60000.0,
      -2000000000.0,
      4000000000.0,
      -9007199254740992.0,
      9007199254740992.0,
  };
  for (int index = 0; index < 8; index++) {
    if (!close_double(converted[index], expected[index]))
      return 3 + index;
  }

  double assigned = 0.0;
  assigned = signed_integer;
  if (assigned != -2000000000.0)
    return 11;
  assigned = unsigned_integer;
  if (assigned != 4000000000.0)
    return 12;

  struct floating_box box = {
      signed_short,
      unsigned_wide,
  };
  if (box.narrow != -30000.0f ||
      box.wide != 9007199254740992.0)
    return 13;

  double_checker checker = close_double;
  if (!close_double(signed_character, -123.0) ||
      !checker(unsigned_short, 60000.0) ||
      !checker(signed_integer, -2000000000.0) ||
      !checker(unsigned_integer, 4000000000.0))
    return 14;

  if (return_signed_character() != -123.0 ||
      return_signed_short() != -30000.0 ||
      return_signed_integer() != -2000000000.0 ||
      return_unsigned_integer() != 4000000000.0 ||
      return_signed_wide() != -9007199254740992.0 ||
      return_unsigned_wide() != 9007199254740992.0)
    return 15;

  double arithmetic = 1.0 + signed_integer;
  double compound = 5.0;
  compound += signed_short;
  double conditional = 1 ? signed_character : signed_integer;
  evaluation_count = 0;
  double comma = (evaluation_count++, unsigned_short);
  if (arithmetic != -1999999999.0 ||
      compound != -29995.0 ||
      conditional != -123.0 ||
      comma != 60000.0 ||
      evaluation_count != 1)
    return 16;

  struct signed_bits bits = {-61};
  double bit_field = bits.value;
  double enumeration = ENUM_NEGATIVE;
  double boolean = (_Bool)1;
  if (bit_field != -61.0 ||
      enumeration != -91.0 ||
      boolean != 1.0)
    return 17;

  evaluation_count = 0;
  if (!checker(next_negative_integer(), -77.0) ||
      evaluation_count != 1)
    return 18;

  return 0;
}
