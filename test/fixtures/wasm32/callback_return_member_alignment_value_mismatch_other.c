struct callback_return_alignment_value_payload {
  _Alignas(4) int value;
};

_Static_assert(
    sizeof(struct callback_return_alignment_value_payload) == sizeof(int),
    "naturally aligned callback return payload size");
_Static_assert(
    _Alignof(struct callback_return_alignment_value_payload) ==
        _Alignof(int),
    "naturally aligned callback return payload alignment");

typedef struct callback_return_alignment_value_payload
    callback_return_alignment_value_factory_t(void);

int invoke_callback_return_alignment_value(
    callback_return_alignment_value_factory_t *factory) {
  struct callback_return_alignment_value_payload value = factory();
  return value.value;
}
