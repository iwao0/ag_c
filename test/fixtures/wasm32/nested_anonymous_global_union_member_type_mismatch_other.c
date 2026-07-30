typedef struct {
  unsigned int guard;
  union {
    const int *pointer;
    unsigned char bytes[4];
    unsigned int value;
  } payload;
} nested_anonymous_global_union_member_type_t;

nested_anonymous_global_union_member_type_t
    nested_anonymous_global_union_member_type_value = {
        .guard = 0U,
        .payload = {.value = 42U},
    };
