/* A file-scope compound literal initializer must be constant. */
int source;
int *pointer = &(int){source};

int main(void) {
  return *pointer;
}
