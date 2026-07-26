/* A visible typedef name is not an identifier in an old-style identifier
 * list, even after an ordinary identifier has selected that grammar. */
typedef int count_t;

int add_count(value, count_t)
int value;
int count_t;
{
  return value + count_t;
}

int main(void) {
  return 0;
}
