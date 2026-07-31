/* Redeclarations of one object cannot request different alignments. */
_Alignas(8) extern int value;
_Alignas(16) int value;

int main(void) {
  return value;
}
