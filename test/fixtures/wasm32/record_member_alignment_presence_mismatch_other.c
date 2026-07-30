struct aligned_presence_payload {
  int value;
};

_Static_assert(
    sizeof(struct aligned_presence_payload) == sizeof(int),
    "aligned and plain definitions must have the same size");
_Static_assert(
    _Alignof(struct aligned_presence_payload) == _Alignof(int),
    "aligned and plain definitions must have the same alignment");

int consume_alignment_presence(struct aligned_presence_payload value) {
  return value.value;
}
