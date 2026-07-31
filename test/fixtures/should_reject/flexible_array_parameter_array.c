struct value {
  int prefix;
  int data[];
};

// Parameter adjustment does not make an invalid array element type valid.
int consume(struct value values[2]) {
  return values != 0;
}

int main(void) {
  return 0;
}
