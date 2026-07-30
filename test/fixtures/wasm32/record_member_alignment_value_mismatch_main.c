struct aligned_value_payload {
  _Alignas(0) int value;
};

_Static_assert(
    sizeof(struct aligned_value_payload) == sizeof(int),
    "zero and natural alignment requests must have the same size");
_Static_assert(
    _Alignof(struct aligned_value_payload) == _Alignof(int),
    "zero and natural alignment requests must have the same alignment");

int consume_alignment_value(struct aligned_value_payload value);

int main(void) {
  struct aligned_value_payload value = {42};
  return consume_alignment_value(value) == 42 ? 0 : 1;
}
