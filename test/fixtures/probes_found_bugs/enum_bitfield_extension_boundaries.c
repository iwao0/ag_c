/*
 * Enum bit-fields must use the signedness of the enum's compatible integer
 * type when a value is extracted from the allocation unit.  In particular,
 * nonnegative values with bit 7 set must be zero-extended, while an enum that
 * has negative enumerators must still be sign-extended.
 */

enum PositiveByte {
  POSITIVE_ZERO = 0,
  POSITIVE_127 = 127,
  POSITIVE_128 = 128,
  POSITIVE_150 = 150,
  POSITIVE_255 = 255
};

enum SignedByte {
  SIGNED_NEGATIVE_128 = -128,
  SIGNED_NEGATIVE_ONE = -1,
  SIGNED_ZERO = 0,
  SIGNED_127 = 127
};

struct PositiveBits {
  enum PositiveByte value : 8;
  unsigned int guard : 4;
};

struct SignedBits {
  enum SignedByte value : 8;
  unsigned int guard : 4;
};

union TreeNode {
  struct {
    union TreeNode *next;
    enum PositiveByte code : 8;
    unsigned int side_effect : 1;
  } common;
};

static struct PositiveBits global_positive = {POSITIVE_255, 9};
static struct SignedBits global_signed = {SIGNED_NEGATIVE_128, 10};

static unsigned int read_positive(struct PositiveBits value) {
  return value.value;
}

static int read_signed(struct SignedBits value) {
  return value.value;
}

static struct PositiveBits return_positive(enum PositiveByte value) {
  struct PositiveBits result = {value, 11};
  return result;
}

static int select_high_code(enum PositiveByte value) {
  switch (value) {
    case POSITIVE_128:
      return 1;
    case POSITIVE_150:
      return 2;
    case POSITIVE_255:
      return 3;
    default:
      return 0;
  }
}

int main(void) {
  struct PositiveBits positive_values[] = {
      {POSITIVE_127, 1},
      {POSITIVE_128, 2},
      {POSITIVE_150, 3},
      {POSITIVE_255, 4},
  };
  struct SignedBits signed_values[] = {
      {SIGNED_NEGATIVE_128, 5},
      {SIGNED_NEGATIVE_ONE, 6},
      {SIGNED_127, 7},
  };
  static struct PositiveBits static_positive = {POSITIVE_128, 8};
  static struct SignedBits static_signed = {SIGNED_NEGATIVE_ONE, 9};
  struct PositiveBits returned = return_positive(POSITIVE_150);
  union TreeNode node = {.common = {0, POSITIVE_150, 1}};
  unsigned int global_positive_value = global_positive.value;
  int global_signed_value = global_signed.value;
  unsigned int positive_value_0 = positive_values[0].value;
  unsigned int positive_value_1 = positive_values[1].value;
  unsigned int positive_value_2 = positive_values[2].value;
  unsigned int positive_value_3 = positive_values[3].value;
  int signed_value_0 = signed_values[0].value;
  int signed_value_1 = signed_values[1].value;
  int signed_value_2 = signed_values[2].value;
  unsigned int static_positive_value = static_positive.value;
  int static_signed_value = static_signed.value;
  unsigned int returned_value = returned.value;
  unsigned int node_code = node.common.code;

  if (global_positive_value != 255u || global_positive.guard != 9)
    return 1;
  if (global_signed_value != -128 || global_signed.guard != 10) return 2;
  if (read_positive(global_positive) != 255u) return 3;
  if (read_signed(global_signed) != -128) return 4;

  if (positive_value_0 != 127u || positive_values[0].guard != 1)
    return 5;
  if (positive_value_1 != 128u || positive_values[1].guard != 2)
    return 6;
  if (positive_value_2 != 150u || positive_values[2].guard != 3)
    return 7;
  if (positive_value_3 != 255u || positive_values[3].guard != 4)
    return 8;

  if (signed_value_0 != -128 || signed_values[0].guard != 5)
    return 9;
  if (signed_value_1 != -1 || signed_values[1].guard != 6)
    return 10;
  if (signed_value_2 != 127 || signed_values[2].guard != 7)
    return 11;

  if (static_positive_value != 128u || static_positive.guard != 8)
    return 12;
  if (static_signed_value != -1 || static_signed.guard != 9) return 13;
  if (returned_value != 150u || returned.guard != 11)
    return 14;
  if (select_high_code(returned.value) != 2) return 15;
  if (node.common.next != 0 || node_code != 150u ||
      node.common.side_effect != 1)
    return 16;
  if (select_high_code(node.common.code) != 2) return 17;

  positive_values[0].value = POSITIVE_255;
  signed_values[2].value = SIGNED_NEGATIVE_128;
  positive_value_0 = positive_values[0].value;
  signed_value_2 = signed_values[2].value;
  if (positive_value_0 != 255u || positive_values[0].guard != 1)
    return 18;
  if (signed_value_2 != -128 || signed_values[2].guard != 7)
    return 19;
  return 0;
}
