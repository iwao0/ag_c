struct global_pointer_layout_payload {
  char tag;
  int value;
};

static struct global_pointer_layout_payload
    global_pointer_layout_storage = {'x', 42};
struct global_pointer_layout_payload *global_pointer_layout_value =
    &global_pointer_layout_storage;
