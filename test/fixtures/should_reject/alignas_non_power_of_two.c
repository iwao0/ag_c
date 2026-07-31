/* A nonzero alignment value must be a valid implementation alignment. */
_Alignas(3) int value;
int main(void) { return value; }
