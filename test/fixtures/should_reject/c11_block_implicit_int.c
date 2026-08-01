/* C11 does not permit an omitted type specifier for a block-scope declaration. */
int block_scope(void) {
  static local;
  return 0;
}
