/* Inferred length does not make an enum array compatible with a wide string. */
enum code {
  CODE_ZERO,
  CODE_ONE
};
typedef enum code code_t;

code_t values[] = L"hi";

int main(void) {
  return 0;
}
