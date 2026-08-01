/* A typedef to enum remains incompatible with a wide-string element type. */
enum code {
  CODE_ZERO,
  CODE_ONE
};
typedef enum code code_t;

code_t values[3] = L"hi";

int main(void) {
  return 0;
}
