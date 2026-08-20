/* A member cannot hide a variably modified pointer behind a typedef. */
void probe(int count) {
  typedef int (*RowPointer)[count];
  struct Item {
    RowPointer values;
  };
}

int main(void) { return 0; }
