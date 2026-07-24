/* A block extern declaration does not introduce a file-scope name. */
int introduce(void) {
  extern int hidden;
  return 0;
}

int main(void) {
  return hidden;
}
