struct callback_parameter_alignment_presence_payload {
  _Alignas(4) int value;
};

_Static_assert(
    sizeof(struct callback_parameter_alignment_presence_payload) ==
        sizeof(int),
    "aligned callback parameter payload size");
_Static_assert(
    _Alignof(struct callback_parameter_alignment_presence_payload) ==
        _Alignof(int),
    "aligned callback parameter payload alignment");

typedef int callback_parameter_alignment_presence_t(
    struct callback_parameter_alignment_presence_payload value);

int invoke_callback_parameter_alignment_presence(
    callback_parameter_alignment_presence_t *callback);

static int read_callback_parameter_alignment_presence(
    struct callback_parameter_alignment_presence_payload value) {
  return value.value;
}

int main(void) {
  return invoke_callback_parameter_alignment_presence(
             read_callback_parameter_alignment_presence) == 42
             ? 0
             : 1;
}
