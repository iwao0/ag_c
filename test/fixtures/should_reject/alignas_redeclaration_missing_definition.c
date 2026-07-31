/* A definition must repeat the alignment requested by an earlier declaration. */
_Alignas(16) extern int value;
int value;

int main(void) {
  return value;
}
