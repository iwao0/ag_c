typedef struct {
  unsigned int guard;
  union {
    const int *pointer;
    unsigned char bytes[4];
    int value;
  } payload;
} nested_anonymous_global_union_signature_t;

nested_anonymous_global_union_signature_t
    nested_anonymous_global_union_signature_value = {
        .guard = 2U,
        .payload = {.value = 40},
    };

int consume_nested_anonymous_global_union(
    nested_anonymous_global_union_signature_t value) {
  return (int)value.guard + value.payload.value;
}
