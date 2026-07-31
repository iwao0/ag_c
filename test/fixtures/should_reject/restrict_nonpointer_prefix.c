/* The restrict qualifier cannot be applied to a non-pointer object type. */
restrict int value;

int main(void) {
  return value;
}
