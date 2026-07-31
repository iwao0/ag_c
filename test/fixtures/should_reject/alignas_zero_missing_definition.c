/* A definition must repeat even a zero alignment request from a declaration. */
_Alignas(0) extern int value;
int value;

int main(void) {
  return value;
}
