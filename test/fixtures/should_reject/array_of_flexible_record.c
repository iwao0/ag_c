/* A structure containing a flexible array cannot be an array element. */
struct flexible {
  int count;
  int values[];
};
struct flexible records[2];
int main(void) { return 0; }
