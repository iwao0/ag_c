#define PASTE(left, right) left ## right
int main(void) { return PASTE(+, *) 1; }
