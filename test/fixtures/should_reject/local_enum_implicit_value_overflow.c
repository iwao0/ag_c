/* Block-scope enum resolution applies the same implicit-value limit. */
int main(void) {
  enum LocalBoundary {
    LOCAL_BOUNDARY_MAX = 2147483647,
    LOCAL_BOUNDARY_NEXT
  };
  return 0;
}
