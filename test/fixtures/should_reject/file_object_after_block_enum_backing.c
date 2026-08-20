/* A hidden backing object for a valid block extern must not mask the
 * file-scope enum constant when a later file declaration reuses the name. */
enum { FileEnumConflict = 1 };

int probe(void) {
  {
    extern int FileEnumConflict;
  }
  return 0;
}

extern int FileEnumConflict;
