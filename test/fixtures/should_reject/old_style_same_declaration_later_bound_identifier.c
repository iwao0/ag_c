/* A declarator cannot use a later identifier from the same old-style parameter declaration. */
int last_value(values, rows)
int values[rows], rows;
{
  return values[0];
}
