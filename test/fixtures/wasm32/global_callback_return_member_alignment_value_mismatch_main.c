struct global_callback_return_alignment_payload {
  _Alignas(0) int value;
};

_Static_assert(
    sizeof(struct global_callback_return_alignment_payload) == sizeof(int),
    "zero-aligned global callback return payload size");
_Static_assert(
    _Alignof(struct global_callback_return_alignment_payload) ==
        _Alignof(int),
    "zero-aligned global callback return payload alignment");

typedef struct global_callback_return_alignment_payload
    global_callback_return_alignment_factory_t(void);

extern global_callback_return_alignment_factory_t
    *global_callback_return_alignment_factory;

int main(void) {
  struct global_callback_return_alignment_payload value =
      global_callback_return_alignment_factory();
  return value.value == 42 ? 0 : 1;
}
