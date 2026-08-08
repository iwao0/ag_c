int main(void) {
  _Alignas(16) extern int aligned_external_value;
  return 0;
}

int aligned_external_value;
