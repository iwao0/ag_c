/* C11 requires at least one initializer inside a braced initializer list. */
int value = {};

int main(void) {
  return value;
}
