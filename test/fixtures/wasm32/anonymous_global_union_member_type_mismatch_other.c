typedef union {
  const int *pointer;
  unsigned char bytes[4];
  unsigned int value;
} anonymous_global_union_member_type_t;

anonymous_global_union_member_type_t
    anonymous_global_union_member_type_value = {.value = 42U};
