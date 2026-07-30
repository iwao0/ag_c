struct incomplete_pointer_context {
  int ignored;
};

struct sibling_layout_payload {
  char tag;
  int values[4];
};

_Static_assert(
    sizeof(struct sibling_layout_payload) == 20,
    "plain sibling payload size");

int read_sibling_layout_payload(
    struct incomplete_pointer_context *context,
    struct sibling_layout_payload value) {
  return context ? context->ignored : value.values[3];
}
