/* A wide string cannot initialize a row whose element type is an enum typedef. */
enum code {
  CODE_ZERO,
  CODE_ONE
};
typedef enum code code_t;

int main(void) {
  code_t rows[1][3] = {L"hi"};
  return rows[0][0];
}
