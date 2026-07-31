// A const-qualified scalar is not a modifiable lvalue.
int main(void) {
  const int value = 5;
  value = 10;
  return value;
}
