/* An automatic array initializer cannot exceed its declared bound. */
int main(void) {
  int values[1] = {1, 2};
  return values[0];
}
