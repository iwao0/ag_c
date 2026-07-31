/* A declaration cannot combine conflicting storage-class specifiers. */
int main(void) { static extern int x; return 0; }
