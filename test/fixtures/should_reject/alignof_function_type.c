/* _Alignof cannot be applied to a function type. */
int main(void) { return (int)_Alignof(int(void)); }
