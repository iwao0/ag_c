/* The variably modified member constraint applies to unions as well. */
void probe(int count) {
  typedef int Row[count];
  union Item {
    Row **values;
  };
}

int main(void) { return 0; }
