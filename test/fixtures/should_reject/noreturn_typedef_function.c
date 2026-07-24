/* _Noreturn cannot be attached to a typedef for a function type. */
typedef _Noreturn int function_type(void);
int main(void) { return 0; }
