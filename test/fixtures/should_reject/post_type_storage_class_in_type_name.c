/* A storage-class specifier is invalid inside a type name. */
int main(void) {
  return sizeof(int static);
}
