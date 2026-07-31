/* A cast target must have void or scalar type, not array type. */
int main(void) { return ((int[2])1)[0]; }
