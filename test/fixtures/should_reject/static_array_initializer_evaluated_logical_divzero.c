// Aggregate initialization must diagnose an evaluated invalid constant expression.
static int values[2] = {
    1 && (1 / 0),
    0 || (1 / 0),
};

int main(void) {
  return values[0];
}
