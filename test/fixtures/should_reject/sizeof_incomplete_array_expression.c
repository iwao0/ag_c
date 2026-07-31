/* sizeof cannot be applied to an expression with incomplete array type. */
extern int values[];

int main(void) {
  return (int)sizeof(values);
}
