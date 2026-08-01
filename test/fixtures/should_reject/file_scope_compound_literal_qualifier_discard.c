/* Initializing a mutable pointer must not discard a compound literal's const qualifier. */
int *pointer = &(const int){1};

int main(void) {
  return *pointer;
}
