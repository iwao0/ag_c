// typedef long size_t; strlen プロトタイプ
// 期待: exit=5
typedef long size_t;
size_t strlen(const char *s);
int main(void) { return (int)strlen("hello"); }
