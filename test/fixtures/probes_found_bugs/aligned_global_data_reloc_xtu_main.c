extern int aligned_global_data_reloc_value;

static int *aligned_global_data_reloc_pointer =
    &aligned_global_data_reloc_value;

int main(void) {
  return aligned_global_data_reloc_pointer ==
                 &aligned_global_data_reloc_value &&
                 *aligned_global_data_reloc_pointer == 42 &&
                 (unsigned long)aligned_global_data_reloc_pointer % 64 == 0
             ? 0
             : 1;
}
