/* Pointer decrement requires a pointer to a complete object type. */
struct incomplete;

int main(void) {
  struct incomplete *pointer = 0;
  --pointer;
  return pointer != 0;
}
