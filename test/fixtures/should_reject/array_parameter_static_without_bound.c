/* 'static' in a parameter array declarator requires a bound expression. */
int consume(int values[static]);

int main(void) {
  return 0;
}
