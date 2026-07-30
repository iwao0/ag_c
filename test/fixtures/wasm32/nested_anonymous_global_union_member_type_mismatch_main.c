typedef struct {
  unsigned int guard;
  union {
    int value;
    unsigned char bytes[4];
    const int *pointer;
  } payload;
} nested_anonymous_global_union_member_type_t;

extern nested_anonymous_global_union_member_type_t
    nested_anonymous_global_union_member_type_value;

int main(void) {
  return nested_anonymous_global_union_member_type_value.payload.value;
}
