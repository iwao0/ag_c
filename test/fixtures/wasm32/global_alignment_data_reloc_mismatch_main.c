extern _Alignas(64) int global_alignment_data_reloc_value;

static int *global_alignment_data_reloc_pointer =
    &global_alignment_data_reloc_value;

int main(void) {
  return *global_alignment_data_reloc_pointer;
}
