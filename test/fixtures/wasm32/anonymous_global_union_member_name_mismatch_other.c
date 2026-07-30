typedef union {
  const int *pointer;
  unsigned char bytes[4];
  int result;
} anonymous_global_union_member_name_t;

anonymous_global_union_member_name_t
    anonymous_global_union_member_name_value = {.result = 42};
