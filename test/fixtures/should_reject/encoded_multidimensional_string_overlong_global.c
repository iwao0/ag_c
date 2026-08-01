/* A UTF-16 row cannot omit more than the final implicit terminator. */
unsigned short rows[1][2] = {u"abc"};

int main(void) {
  return 0;
}
