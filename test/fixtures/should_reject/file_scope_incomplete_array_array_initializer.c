/* An array expression cannot infer an incomplete file-scope array bound. */
int source[2] = {1, 2};
int values[] = source;

int main(void) {
  return values[0];
}
