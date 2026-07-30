struct pointer_layout_payload {
  char tag;
  int value;
};

_Static_assert(
    sizeof(struct pointer_layout_payload) == 8,
    "plain pointer payload size");

int read_pointer_layout_payload(
    const struct pointer_layout_payload *value) {
  return value->value;
}
