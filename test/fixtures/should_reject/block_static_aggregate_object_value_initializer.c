/* A block-scope static aggregate member still requires a constant initializer. */
struct pair {
  int value;
};

static struct pair source = {1};

int main(void) {
  static struct pair copy = {source.value};
  return copy.value;
}
