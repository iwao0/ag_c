union qualified_payload {
  unsigned int low : 7;
  volatile int *pointer;
  int integer;
};

int consume_qualified_payload(union qualified_payload value) {
  return value.integer;
}
