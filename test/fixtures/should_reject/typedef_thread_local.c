/* typedef and _Thread_local are conflicting storage classes. */
typedef _Thread_local int value_type;
int main(void) { return 0; }
