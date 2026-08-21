/* A pointer to an atomic structure is not valid for the -> operator. */
struct pair {
  int value;
};

_Atomic(struct pair) shared_pair = (struct pair){7};

int main(void) {
  _Atomic(struct pair) *pointer = &shared_pair;
  return pointer->value;
}
