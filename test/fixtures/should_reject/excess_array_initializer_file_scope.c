/* A file-scope array initializer cannot exceed its declared bound. */
int values[1] = {1, 2};

int main(void) {
  return values[0];
}
