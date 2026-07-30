// A signed-compatible enumeration return type is not compatible with
// unsigned int even though both lower to the same Wasm i32 result.

enum signed_result {
  SIGNED_RESULT_NEGATIVE = -1,
  SIGNED_RESULT_VALUE = 42
};

enum signed_result incompatible_result(void);

int main(void) {
  return incompatible_result();
}
