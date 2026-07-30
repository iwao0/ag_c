struct aligned_value_payload {
  _Alignas(4) int value;
};

_Static_assert(
    sizeof(struct aligned_value_payload) == sizeof(int),
    "zero and natural alignment requests must have the same size");
_Static_assert(
    _Alignof(struct aligned_value_payload) == _Alignof(int),
    "zero and natural alignment requests must have the same alignment");

int consume_alignment_value(struct aligned_value_payload value) {
  return value.value;
}
