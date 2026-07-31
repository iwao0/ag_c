/* A parameter array declarator cannot contain duplicate 'static' qualifiers. */
static int consume(int values[static static 3]);

int main(void) {
  return 0;
}
