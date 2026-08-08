int prototype_enum_declaration(
    enum prototype_enum { PROTOTYPE_ONLY_VALUE = 7 } *value);

int escaped_value = PROTOTYPE_ONLY_VALUE;

int main(void) {
  return escaped_value;
}
