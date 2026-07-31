/* A parameter array bound cannot combine 'static' with '*'. */
int consume(int values[static *]);

int main(void) {
  return 0;
}
