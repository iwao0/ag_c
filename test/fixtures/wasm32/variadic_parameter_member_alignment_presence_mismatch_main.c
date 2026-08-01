// The fixed parameter of a variadic function retains recursive member
// alignment metadata in its cross-TU signature.
struct variadic_alignment_payload {
  _Alignas(4) int value;
};

_Static_assert(
    sizeof(struct variadic_alignment_payload) == sizeof(int),
    "aligned and plain definitions must have the same size");
_Static_assert(
    _Alignof(struct variadic_alignment_payload) == _Alignof(int),
    "aligned and plain definitions must have the same alignment");

int consume_variadic_parameter_alignment(
    int marker, struct variadic_alignment_payload value, ...);

int main(void) {
  struct variadic_alignment_payload value = {20};
  return consume_variadic_parameter_alignment(1, value, 21) == 42 ? 0 : 1;
}
