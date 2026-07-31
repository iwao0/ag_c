/* A scalar parameter cannot be restrict-qualified. */
int consume(int restrict value) {
  return value;
}

int main(void) {
  return consume(0);
}
