extern int aligned_global_definition_value;

int main(void) {
  return aligned_global_definition_value == 42 &&
                 (unsigned long)&aligned_global_definition_value % 64 == 0
             ? 0
             : 1;
}
