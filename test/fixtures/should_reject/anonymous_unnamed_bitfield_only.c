/* An anonymous aggregate must recursively contribute a named member. */
struct Outer {
  struct {
    unsigned int : 1;
  };
};

int main(void) { return 0; }
