// 論理 OR よりも AND が優先 (1 || (0 && 0)) = 1
// 期待: exit=1
main() { return 1||0&&0; }
