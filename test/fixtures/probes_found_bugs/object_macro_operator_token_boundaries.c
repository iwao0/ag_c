/*
 * Object-like macro replacement preserves preprocessing-token boundaries.
 * Tokens from the replacement list and the surrounding source are rescanned,
 * but they are not lexed again into ++, --, or a comment delimiter.
 */

#define ACPI_TYPE_INVALID 0x1E
#define NUM_NS_TYPES ACPI_TYPE_INVALID+1

#define HEX_VALUE 0xe
#define HEX_ALIAS HEX_VALUE
#define SUM 3 + 4
#define PLUS +
#define MINUS -
#define SLASH /
#define STAR *
#define OPEN (
#define CLOSE )
#define CALL_TARGET add_one

static int namespace_objects[NUM_NS_TYPES];

enum {
  ENUM_NAMESPACE_COUNT = NUM_NS_TYPES,
  ENUM_HEX_SUCCESSOR = HEX_ALIAS+1
};

_Static_assert(NUM_NS_TYPES == 31, "macro array bound");
_Static_assert(ENUM_NAMESPACE_COUNT == 31, "macro enum value");
_Static_assert(ENUM_HEX_SUCCESSOR == 15, "adjacent plus token");
_Static_assert(sizeof(namespace_objects) / sizeof(namespace_objects[0]) == 31,
               "macro-sized object");

static int add_one(int value) {
  return value + 1;
}

int main(void) {
  int divisor = 2;
  int adjacent_right = HEX_VALUE+1;
  int adjacent_left = 1+HEX_VALUE;
  int alias_rescan = HEX_ALIAS+1;
  int unparenthesized_precedence = SUM * 2;
  int source_parenthesized = (SUM) * 2;
  int split_increment = 1 PLUS +2;
  int split_decrement = 1 MINUS -2;
  int split_comment_left = 8 SLASH *&divisor;
  int split_comment_right = 8 / STAR &divisor;
  int macro_parentheses = OPEN 2 + 3 CLOSE * 4;
  int macro_function_designator = CALL_TARGET(8);

  namespace_objects[NUM_NS_TYPES - 1] = 31;

  if (adjacent_right != 15) return 1;
  if (adjacent_left != 15) return 2;
  if (alias_rescan != 15) return 3;
  if (unparenthesized_precedence != 11) return 4;
  if (source_parenthesized != 14) return 5;
  if (split_increment != 3) return 6;
  if (split_decrement != 3) return 7;
  if (split_comment_left != 4) return 8;
  if (split_comment_right != 4) return 9;
  if (macro_parentheses != 20) return 10;
  if (macro_function_designator != 9) return 11;
  if (namespace_objects[30] != 31) return 12;
  return 0;
}
