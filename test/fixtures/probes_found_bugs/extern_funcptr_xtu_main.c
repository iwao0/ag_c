// Cross-TU function address through a local function pointer.
// Expected with extern_funcptr_xtu_other.c: exit=42

int other(int);

int main(void) {
    int (*fp)(int) = other;
    return fp(41);
}
