// A comma expression is not an integer constant expression for an enumerator.
enum values {
  VALUE = (1, 2)
};

int main(void) {
  return 0;
}
