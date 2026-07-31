/* A structure member cannot have incomplete record type. */
struct incomplete;
struct record { struct incomplete member; };
int main(void) { return 0; }
