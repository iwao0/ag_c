/* An alignment request cannot weaken an object's natural alignment. */
_Alignas(1) int value;
int main(void) { return value; }
