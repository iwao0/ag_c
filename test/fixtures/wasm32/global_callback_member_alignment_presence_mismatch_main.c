struct global_callback_alignment_payload {
  _Alignas(4) int value;
};

_Static_assert(
    sizeof(struct global_callback_alignment_payload) == sizeof(int),
    "aligned global callback payload size");
_Static_assert(
    _Alignof(struct global_callback_alignment_payload) == _Alignof(int),
    "aligned global callback payload alignment");

typedef int global_callback_member_alignment_t(
    struct global_callback_alignment_payload value);

extern global_callback_member_alignment_t
    *global_callback_member_alignment;

int main(void) {
  struct global_callback_alignment_payload value = {42};
  return global_callback_member_alignment(value) == 42 ? 0 : 1;
}
