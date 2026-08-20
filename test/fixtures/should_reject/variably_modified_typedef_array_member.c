/* A structure member cannot have a variably modified array typedef type. */
void probe(int count) {
  typedef int Row[count];
  struct Item {
    Row values;
  };
}

int main(void) { return 0; }
