/* Compound pointer addition cannot use a pointer to an incomplete record. */
struct incomplete;

int main(void) {
  struct incomplete *pointer = 0;
  pointer += 1;
  return pointer != 0;
}
