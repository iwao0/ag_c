/* Member access must name a member declared by the record type. */
struct record { int known; };
int main(void) { struct record value = {0}; return value.unknown; }
