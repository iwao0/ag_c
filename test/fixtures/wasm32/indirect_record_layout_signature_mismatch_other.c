struct indirect_layout_payload {
  char tag;
  int values[4];
};

_Static_assert(
    sizeof(struct indirect_layout_payload) == 20,
    "plain indirect payload size");

int read_indirect_layout_payload(
    struct indirect_layout_payload value) {
  return value.values[3];
}
