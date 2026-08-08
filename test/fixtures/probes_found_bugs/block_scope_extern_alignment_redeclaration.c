_Alignas(16) int aligned_external_value = 42;

int main(void) {
  _Alignas(16) extern int aligned_external_value;
  return aligned_external_value == 42 ? 0 : 1;
}
