/* A built-in scalar type cannot be restrict-qualified in a type name. */
int main(void) {
  return sizeof(restrict int);
}
