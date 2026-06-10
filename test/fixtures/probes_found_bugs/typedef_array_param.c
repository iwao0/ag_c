// typedef of array
typedef int Vec3[3];
int sum(Vec3 v) { return v[0] + v[1] + v[2]; }
int main(void) {
  Vec3 a = {1, 2, 3};
  return sum(a);
}
// 期待: 6
