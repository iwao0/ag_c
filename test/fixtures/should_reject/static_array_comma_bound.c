// A file-scope array bound cannot use a comma expression as an ICE.
static int values[(1, 2)];

int main(void) {
  return values[0];
}
