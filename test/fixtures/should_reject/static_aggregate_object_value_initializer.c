/* A static aggregate member cannot be initialized from another object's value. */
struct pair {
  int value;
};

static struct pair source = {1};
static struct pair copy = {source.value};

int main(void) {
  return copy.value;
}
