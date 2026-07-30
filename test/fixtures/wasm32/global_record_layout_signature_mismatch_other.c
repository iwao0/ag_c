struct global_record_layout_payload {
  char tag;
  int value;
};

_Static_assert(
    sizeof(struct global_record_layout_payload) == 8,
    "plain global payload size");

struct global_record_layout_payload
    global_record_layout_value = {'x', 42};
