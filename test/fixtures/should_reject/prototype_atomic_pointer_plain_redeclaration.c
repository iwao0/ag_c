/* Redeclarations cannot change a plain pointer parameter into an atomic pointer. */
int atomic_value(int *value);
int atomic_value(int *_Atomic value);
