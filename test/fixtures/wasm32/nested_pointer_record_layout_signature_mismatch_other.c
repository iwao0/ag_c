struct nested_pointer_payload {
  char tag;
  int value;
};

struct nested_pointer_envelope {
  const struct nested_pointer_payload *payload;
  long long outer_layout_anchor;
};

_Static_assert(
    sizeof(struct nested_pointer_payload) == 8,
    "plain nested pointer payload size");
_Static_assert(
    _Alignof(struct nested_pointer_envelope) == _Alignof(long long),
    "outer envelope alignment must match in both translation units");

int read_nested_pointer_envelope(
    const struct nested_pointer_envelope *value) {
  return value->payload->value;
}
