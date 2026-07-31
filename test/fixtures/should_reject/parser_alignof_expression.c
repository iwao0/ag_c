/* C11 _Alignof accepts a type name, not an expression. */
int main(void) { return (int)_Alignof(1); }
