// 後置デクリメント
// a-- 後 a=2、 b=旧値=3、 return 2*10+3 = 23
// 期待: exit=23
main() { a=3; b=a--; return a*10+b; }
