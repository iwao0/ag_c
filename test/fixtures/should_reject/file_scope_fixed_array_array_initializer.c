/* A file-scope array cannot be initialized from another array expression. */
int source[2] = {1, 2};
int values[2] = source;

int main(void) {
  return values[0];
}
