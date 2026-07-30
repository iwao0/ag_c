typedef union {
  int value;
  unsigned char bytes[4];
  const int *pointer;
} anonymous_global_union_member_name_t;

extern anonymous_global_union_member_name_t
    anonymous_global_union_member_name_value;

int main(void) {
  return anonymous_global_union_member_name_value.value;
}
