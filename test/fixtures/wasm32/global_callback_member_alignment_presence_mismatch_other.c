struct global_callback_alignment_payload {
  int value;
};

_Static_assert(
    sizeof(struct global_callback_alignment_payload) == sizeof(int),
    "plain global callback payload size");
_Static_assert(
    _Alignof(struct global_callback_alignment_payload) == _Alignof(int),
    "plain global callback payload alignment");

typedef int global_callback_member_alignment_t(
    struct global_callback_alignment_payload value);

static int read_global_callback_alignment_payload(
    struct global_callback_alignment_payload value) {
  return value.value;
}

global_callback_member_alignment_t *global_callback_member_alignment =
    read_global_callback_alignment_payload;
