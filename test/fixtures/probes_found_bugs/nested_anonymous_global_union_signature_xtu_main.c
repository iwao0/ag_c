typedef struct {
  unsigned int guard;
  union {
    int value;
    unsigned char bytes[4];
    const int *pointer;
  } payload;
} nested_anonymous_global_union_signature_t;

extern nested_anonymous_global_union_signature_t
    nested_anonymous_global_union_signature_value;

int consume_nested_anonymous_global_union(
    nested_anonymous_global_union_signature_t value);

int main(void) {
  return consume_nested_anonymous_global_union(
      nested_anonymous_global_union_signature_value);
}
