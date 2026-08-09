/* An incomplete array object is not a modifiable assignment target. */
extern int source[];
extern int values[];

int main(void) {
  values = source;
  return 0;
}
