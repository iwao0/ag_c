enum global_signed_compatible_value {
  GLOBAL_SIGNED_COMPATIBLE_NEGATIVE = -1,
  GLOBAL_SIGNED_COMPATIBLE_VALUE = 19
};

enum global_unsigned_compatible_value {
  GLOBAL_UNSIGNED_COMPATIBLE_ZERO = 0,
  GLOBAL_UNSIGNED_COMPATIBLE_VALUE = 23
};

extern enum global_signed_compatible_value
    global_signed_compatible_value;
extern enum global_unsigned_compatible_value
    global_unsigned_compatible_value;

int main(void) {
  return global_signed_compatible_value +
         (int)global_unsigned_compatible_value;
}
