/* A scalar initializer cannot contain a nested initializer list. */
int value = {{7}};

int main(void) {
  return value;
}
