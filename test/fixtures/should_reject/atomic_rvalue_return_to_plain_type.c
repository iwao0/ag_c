_Atomic int source(void) { return 1; }
int sink(void) { return source(); }
int main(void) { return 0; }
