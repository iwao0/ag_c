/* A function definition cannot have an incomplete record parameter by value. */
struct record;

int function(struct record value) {
  return 0;
}

int main(void) {
  return 0;
}
