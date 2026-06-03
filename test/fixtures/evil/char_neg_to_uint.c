// signed char -1 → unsigned char → unsigned int で 255
// 期待: exit=1
int main(void) {
    char c = -1;
    unsigned int u = (unsigned int)(unsigned char)c;
    return u == 255;
}
