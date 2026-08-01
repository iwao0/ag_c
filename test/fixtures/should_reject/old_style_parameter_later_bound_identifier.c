/* An old-style array parameter bound cannot refer to parameters declared later. */
int matrix(values, rows, columns)
int values[rows][columns];
int rows;
int columns;
{
  return 0;
}
