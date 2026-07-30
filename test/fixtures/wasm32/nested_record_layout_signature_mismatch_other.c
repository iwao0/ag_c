struct nested_layout_payload {
  char tag;
  int value;
};

union nested_layout_envelope {
  struct nested_layout_payload payload;
  long long force_outer_layout;
};

_Static_assert(
    sizeof(struct nested_layout_payload) == 8,
    "plain nested payload size");
_Static_assert(
    sizeof(union nested_layout_envelope) == sizeof(long long),
    "outer union size must match in both translation units");
_Static_assert(
    _Alignof(union nested_layout_envelope) == _Alignof(long long),
    "outer union alignment must match in both translation units");

int read_nested_layout_envelope(
    union nested_layout_envelope value) {
  return value.payload.value;
}
