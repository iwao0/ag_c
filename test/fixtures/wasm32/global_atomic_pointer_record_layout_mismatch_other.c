struct global_atomic_layout_payload {
  char tag;
  int value;
};

static struct global_atomic_layout_payload value = {'x', 42};

_Atomic(struct global_atomic_layout_payload *)
    global_atomic_pointer_record_layout = &value;
