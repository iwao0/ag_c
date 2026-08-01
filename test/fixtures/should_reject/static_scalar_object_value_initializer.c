/* A static scalar cannot be initialized from another object's value. */
static int source = 1;
static int copy = source;

int main(void) {
  return copy;
}
