/* _Alignof cannot be applied to an incomplete object type. */
struct incomplete;
int main(void) { return (int)_Alignof(struct incomplete); }
