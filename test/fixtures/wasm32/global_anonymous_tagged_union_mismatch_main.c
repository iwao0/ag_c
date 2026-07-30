typedef union {
  int value;
  unsigned char bytes[4];
} anonymous_global_union_t;

extern anonymous_global_union_t
    global_anonymous_tagged_union_value;

int main(void) {
  return global_anonymous_tagged_union_value.value;
}
