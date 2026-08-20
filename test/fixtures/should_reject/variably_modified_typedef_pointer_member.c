/* A pointer derived from a VLA typedef is still variably modified. */
void probe(int count) {
  typedef int Row[count];
  struct Item {
    Row *values;
  };
}

int main(void) { return 0; }
