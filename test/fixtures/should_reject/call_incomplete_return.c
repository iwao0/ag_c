/* A call cannot produce a value of incomplete record type. */
struct record;

struct record make(void);

int main(void) {
  make();
  return 0;
}
