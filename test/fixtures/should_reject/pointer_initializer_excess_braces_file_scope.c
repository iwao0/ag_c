/* Pointer scalar initialization follows the same single-brace constraint. */
int target;
int *pointer = {{&target}};

int main(void) {
  return pointer != 0;
}
