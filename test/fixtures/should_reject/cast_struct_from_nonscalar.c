int main(void) {
  struct S { int x; };
  union U { int y; };
  union U value = {1};
  return (struct S)value;
}
