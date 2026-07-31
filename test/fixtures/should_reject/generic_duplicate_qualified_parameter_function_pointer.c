typedef int (*plain_callback)(int);
typedef int (*qualified_parameter_callback)(const int);

int main(void) {
  return _Generic(
      (plain_callback)0,
      plain_callback: 1,
      qualified_parameter_callback: 2,
      default: 0);
}
