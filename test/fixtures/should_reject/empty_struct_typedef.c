/* A typedef declarator does not make its empty structure body valid. */
typedef struct Empty {} Empty;

int main(void) { return 0; }
