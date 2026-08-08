_Alignas(16) int aligned_external_value;

int main(void) {
  _Alignas(32) extern int aligned_external_value;
  return aligned_external_value;
}
