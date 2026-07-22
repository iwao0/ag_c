enum values {
  ENUM_CAST = (int)1.9,
  ENUM_UNSELECTED_COMMA_TRUE = 1 ? 2 : (1, 2),
  ENUM_UNSELECTED_COMMA_FALSE = 0 ? (1, 2) : 3
};

struct bits {
  unsigned value : (int)3.9;
};

_Alignas((int)16.9) int aligned_value;
int array_value[(int)2.9];
static int initialized_value = (int)4.9;

int main(void) {
  switch ((int)1.9) {
    case (int)1.9:
      return ENUM_CAST == 1 &&
             ENUM_UNSELECTED_COMMA_TRUE == 2 &&
             ENUM_UNSELECTED_COMMA_FALSE == 3 &&
             sizeof(struct bits) == sizeof(unsigned) &&
             sizeof(array_value) == 2 * sizeof(int) &&
             initialized_value == 4
                 ? 0
                 : 1;
  }
  return 2;
}
