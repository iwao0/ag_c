// _Alignas requires an integer constant expression, not a comma expression.
_Alignas((1, 16)) int value;

int main(void) {
  return 0;
}
