// typedef でポインタ型 alias
// 期待: exit=11
typedef int *intptr;
int main(void) {
    int a = 11;
    intptr p = &a;
    return *p;
}
