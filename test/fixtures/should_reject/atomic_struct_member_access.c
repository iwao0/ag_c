/* An atomic structure is not a structure operand for direct member access. */
struct pair {
  int value;
};

_Atomic(struct pair) shared_pair = (struct pair){7};

int main(void) {
  return shared_pair.value;
}
