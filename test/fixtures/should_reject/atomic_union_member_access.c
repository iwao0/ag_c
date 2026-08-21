/* An atomic union is not a union operand for direct member access. */
union word {
  int integer;
  unsigned bits;
};

_Atomic(union word) shared_word = (union word){.integer = 7};

int main(void) {
  return shared_word.integer;
}
