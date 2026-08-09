/* A fixed-size file-scope array cannot use a scalar expression initializer. */
int values[2] = 1;

int main(void) {
  return values[0];
}
