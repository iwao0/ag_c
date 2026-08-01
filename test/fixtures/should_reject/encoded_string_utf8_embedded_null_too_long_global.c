/* An explicit NUL is content and cannot be dropped like the implicit terminator. */
char values[4] = u8"\U0001F600\0";

int main(void) {
  return 0;
}
