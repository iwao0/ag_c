/* An atomic character type is not a permitted string-literal array element type. */
_Atomic char values[3] = "hi";

int main(void) {
  return 0;
}
