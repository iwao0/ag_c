// A cast of a compound floating expression is not an integer constant expression.
enum values {
  VALUE = (int)(1.0 + 2.0)
};

int main(void) {
  return 0;
}
