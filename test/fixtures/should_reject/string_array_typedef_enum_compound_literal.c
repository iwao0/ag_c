/* An enum-array compound literal cannot use a wide-string initializer. */
enum code {
  CODE_ZERO,
  CODE_ONE
};
typedef enum code code_t;

int main(void) {
  code_t *values = (code_t[3]){L"hi"};
  return values[0];
}
