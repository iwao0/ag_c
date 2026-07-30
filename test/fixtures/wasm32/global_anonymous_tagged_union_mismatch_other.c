union tagged_global_union {
  int value;
  unsigned char bytes[4];
};

union tagged_global_union
    global_anonymous_tagged_union_value = {.value = 42};
