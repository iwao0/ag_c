enum global_expected_enum_type {
  GLOBAL_EXPECTED_ENUM_ZERO = 0,
  GLOBAL_EXPECTED_ENUM_VALUE = 42
};

extern enum global_expected_enum_type
    global_distinct_enum_type_value;

int main(void) {
  return global_distinct_enum_type_value;
}
