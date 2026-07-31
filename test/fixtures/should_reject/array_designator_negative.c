/* An array designator index cannot be negative. */
int values[3] = {[-1] = 7};

int main(void) {
  return values[0];
}
