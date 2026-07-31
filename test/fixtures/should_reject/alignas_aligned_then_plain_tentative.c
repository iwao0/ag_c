/* Every definition must repeat an alignment requested by another declaration. */
_Alignas(16) int value;
int value;

int main(void) {
  return value;
}
