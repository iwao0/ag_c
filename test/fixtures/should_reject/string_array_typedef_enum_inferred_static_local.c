/* A static local inferred enum array remains incompatible with a wide string. */
enum code {
  CODE_ZERO,
  CODE_ONE
};
typedef enum code code_t;

int main(void) {
  static code_t values[] = L"hi";
  return values[0];
}
