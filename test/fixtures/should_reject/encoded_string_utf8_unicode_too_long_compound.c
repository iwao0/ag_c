/* A compound literal cannot truncate required UTF-8 code units. */
int main(void) {
  char *values = (char[3]){u8"\U0001F600"};
  return values[0];
}
