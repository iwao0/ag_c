struct global_callback_layout_payload {
  char tag;
  int value;
};

typedef int global_callback_layout_reader_t(
    const struct global_callback_layout_payload *value);

static int read_global_callback_layout_payload(
    const struct global_callback_layout_payload *value) {
  return value->value;
}

global_callback_layout_reader_t *global_callback_layout_reader =
    read_global_callback_layout_payload;
