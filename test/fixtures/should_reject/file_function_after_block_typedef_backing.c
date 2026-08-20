/* The hidden backing symbol for the valid block declaration must not mask
 * the file-scope typedef when a later file declaration reuses the name. */
typedef int FileConflict;

int probe(void) {
  {
    int FileConflict(FileConflict);
  }
  return 0;
}

int FileConflict(int value);
