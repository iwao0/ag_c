/* The name introduced by a block extern declaration ends with that block. */
int main(void) {
  {
    extern int hidden;
  }
  return hidden;
}
