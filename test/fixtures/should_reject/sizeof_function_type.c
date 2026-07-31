/* sizeof cannot be applied to a function type. */
int main(void) {
  return (int)sizeof(int(void));
}
