/* A named inline record definition still requires a member declarator. */
void probe(void) {
  struct Outer {
    struct Inner {
      int value;
    };
  };
}

int main(void) { return 0; }
