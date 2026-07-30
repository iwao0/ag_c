struct callback_pointer_layout_payload {
  char tag;
  int value;
};

_Static_assert(
    sizeof(struct callback_pointer_layout_payload) == 8,
    "plain callback pointer payload size");

typedef int callback_pointer_layout_reader_t(
    const struct callback_pointer_layout_payload *value);

int invoke_callback_pointer_layout_reader(
    callback_pointer_layout_reader_t *reader) {
  struct callback_pointer_layout_payload value = {'x', 42};
  return reader(&value);
}
