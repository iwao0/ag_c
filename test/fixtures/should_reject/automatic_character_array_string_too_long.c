/* An automatic character array may omit only the implicit terminator. */
int main(void) {
  char text[2] = "abc";
  return text[0];
}
