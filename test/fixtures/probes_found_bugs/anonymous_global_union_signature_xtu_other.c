typedef union {
  const int *pointer;
  unsigned char bytes[4];
  int value;
} anonymous_global_union_signature_t;

anonymous_global_union_signature_t
    anonymous_global_union_signature_value = {.value = 42};
