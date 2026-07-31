/* A function definition cannot return an incomplete record by value. */
struct record;

struct record function(void) {
  for (;;) {
  }
}

int main(void) {
  return 0;
}
