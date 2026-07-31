/* inline is invalid in a function type name. */
int main(void) {
  return sizeof(inline int (void));
}
