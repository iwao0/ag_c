// ポインタの相対アクセス: *(p-1), *(p+0), *(p+1)
// arr={100,200,300}, p=arr+1
// 期待: exit=88
#include <assert.h>
int main(void) {
    int arr[3];
    arr[0]=100; arr[1]=200; arr[2]=300;
    int *p = arr + 1;
    assert(*(p-1) == 100);
    assert(*(p+0) == 200);
    assert(*(p+1) == 300);
    return 0;
}
