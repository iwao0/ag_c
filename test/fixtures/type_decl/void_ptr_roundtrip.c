// void* を介した int* のラウンドトリップ
// 期待: exit=5
int main(void) {
    int x = 5;
    void *v = &x;
    int *p = (int*)v;
    return *p;
}
