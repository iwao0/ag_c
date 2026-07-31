/* A function cannot return a value with a distinct structure type. */
struct first {
  int value;
};

struct second {
  int value;
};

static struct first get(void) {
  struct second value = {1};
  return value;
}

int main(void) {
  return get().value;
}
