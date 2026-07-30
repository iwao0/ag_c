struct global_atomic_member_payload {
  unsigned int value;
};

static struct global_atomic_member_payload value = {42};

_Atomic(struct global_atomic_member_payload *)
    global_atomic_pointer_record_member = &value;
