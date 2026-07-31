/* _Alignof cannot be applied to an incomplete array type. */
int main(void) {
  return (int)_Alignof(int[]);
}
