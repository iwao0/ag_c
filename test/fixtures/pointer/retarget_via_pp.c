// **pp を介して p の指す先を切り替える
// *pp=&b で p=&b → *p=2
// 期待: exit=2
int main(void) {
    int a = 1;
    int b = 2;
    int *p = &a;
    int **pp = &p;
    *pp = &b;
    return *p;
}
