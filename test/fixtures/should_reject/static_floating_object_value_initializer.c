/* A static floating object cannot be initialized from another object's value. */
static double source = 1.0;
static double copy = source;

int main(void) {
  return copy != 0.0;
}
