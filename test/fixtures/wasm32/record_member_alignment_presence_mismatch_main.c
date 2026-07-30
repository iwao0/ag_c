struct aligned_presence_payload {
  _Alignas(4) int value;
};

_Static_assert(
    sizeof(struct aligned_presence_payload) == sizeof(int),
    "aligned and plain definitions must have the same size");
_Static_assert(
    _Alignof(struct aligned_presence_payload) == _Alignof(int),
    "aligned and plain definitions must have the same alignment");

int consume_alignment_presence(struct aligned_presence_payload value);

int main(void) {
  struct aligned_presence_payload value = {42};
  return consume_alignment_presence(value) == 42 ? 0 : 1;
}
