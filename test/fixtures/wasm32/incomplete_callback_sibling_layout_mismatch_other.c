struct incomplete_callback_sibling_context {
  int ignored;
};

struct callback_sibling_layout_payload {
  char tag;
  int values[4];
};

_Static_assert(
    sizeof(struct callback_sibling_layout_payload) == 20,
    "plain callback sibling payload size");

typedef int incomplete_callback_sibling_reader_t(
    struct incomplete_callback_sibling_context *context,
    struct callback_sibling_layout_payload value);

int invoke_incomplete_callback_sibling_reader(
    incomplete_callback_sibling_reader_t *reader) {
  struct callback_sibling_layout_payload value = {
      'x', {10, 20, 30, 42}};
  return reader(0, value);
}
