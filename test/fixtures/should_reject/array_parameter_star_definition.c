/* A [*] parameter array declarator is limited to function declarations. */
int read_values(int values[*]) {
  return values[0];
}

int main(void) {
  return 0;
}
