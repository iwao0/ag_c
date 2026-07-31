/* An object that does not have function or function-pointer type is not callable. */
int main(void) {
  int value = 1;
  return value();
}
