union qualified_payload {
  int integer;
  const int *pointer;
  unsigned int low : 7;
};

int consume_qualified_payload(union qualified_payload value);

int main(void) {
  union qualified_payload value;
  value.integer = 42;
  return consume_qualified_payload(value) == 42 ? 0 : 1;
}
