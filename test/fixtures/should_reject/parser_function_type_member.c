/* A structure member cannot have function type. */
struct record { int function(void); };
int main(void) { return 0; }
