/* sizeof cannot be applied to an incomplete array type. */
int main(void) {
  return (int)sizeof(int[]);
}
