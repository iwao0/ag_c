// Static local declarations use the static-initializer pipeline too.
int main(void) {
  static char text[2] = "abc";
  return text[0];
}
