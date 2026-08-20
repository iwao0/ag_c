/* A hidden backing object for a valid block extern must not mask the
 * file-scope typedef when a later file declaration reuses the name. */
typedef int FileObjectConflict;

int probe(void) {
  {
    extern int FileObjectConflict;
  }
  return 0;
}

int FileObjectConflict;
