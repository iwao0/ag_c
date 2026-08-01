struct global_callback_return_alignment_payload {
  _Alignas(4) int value;
};

_Static_assert(
    sizeof(struct global_callback_return_alignment_payload) == sizeof(int),
    "naturally aligned global callback return payload size");
_Static_assert(
    _Alignof(struct global_callback_return_alignment_payload) ==
        _Alignof(int),
    "naturally aligned global callback return payload alignment");

typedef struct global_callback_return_alignment_payload
    global_callback_return_alignment_factory_t(void);

static struct global_callback_return_alignment_payload
make_global_callback_return_alignment_payload(void) {
  struct global_callback_return_alignment_payload value = {42};
  return value;
}

global_callback_return_alignment_factory_t
    *global_callback_return_alignment_factory =
        make_global_callback_return_alignment_payload;
