struct callback_parameter_alignment_presence_payload {
  int value;
};

_Static_assert(
    sizeof(struct callback_parameter_alignment_presence_payload) ==
        sizeof(int),
    "plain callback parameter payload size");
_Static_assert(
    _Alignof(struct callback_parameter_alignment_presence_payload) ==
        _Alignof(int),
    "plain callback parameter payload alignment");

typedef int callback_parameter_alignment_presence_t(
    struct callback_parameter_alignment_presence_payload value);

int invoke_callback_parameter_alignment_presence(
    callback_parameter_alignment_presence_t *callback) {
  struct callback_parameter_alignment_presence_payload value = {42};
  return callback(value);
}
