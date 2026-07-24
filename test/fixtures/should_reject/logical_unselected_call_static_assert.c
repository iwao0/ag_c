static int runtime_value(void) {
  return 1;
}

_Static_assert(0 && runtime_value(),
               "function call is not an integer constant expression operand");

int main(void) {
  return 0;
}
