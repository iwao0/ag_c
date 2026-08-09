/* An incomplete file-scope array needs an initializer that infers its bound. */
int values[] = 1;

int main(void) {
  return values[0];
}
