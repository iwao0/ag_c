struct callback_return_alignment_value_payload {
  _Alignas(0) int value;
};

_Static_assert(
    sizeof(struct callback_return_alignment_value_payload) == sizeof(int),
    "zero-aligned callback return payload size");
_Static_assert(
    _Alignof(struct callback_return_alignment_value_payload) ==
        _Alignof(int),
    "zero-aligned callback return payload alignment");

typedef struct callback_return_alignment_value_payload
    callback_return_alignment_value_factory_t(void);

int invoke_callback_return_alignment_value(
    callback_return_alignment_value_factory_t *factory);

static struct callback_return_alignment_value_payload
make_callback_return_alignment_value(void) {
  struct callback_return_alignment_value_payload value = {42};
  return value;
}

int main(void) {
  return invoke_callback_return_alignment_value(
             make_callback_return_alignment_value) == 42
             ? 0
             : 1;
}
