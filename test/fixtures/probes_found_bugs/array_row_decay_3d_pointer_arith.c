// 3 次元以上の多次元配列で、中間の「行」(要素自体がさらに配列) を値文脈の
// ポインタ算術に使うケース。`int t[2][2][2]` の `t[i]` は int[2][2] (平面) で
// int(*)[2] へ decay、`*(t[i]+j)` は int[2] (行) で int* へ decay する。
// 旧実装は (1) add() が中間行をポインタと認識せず添字を要素サイズでスケールしない、
// (2) build_unary_deref が行全体 8B 以下のとき内側ストライドを引き継がず、
// (3) build_node_deref が type_size>8 の行しか崩壊させない、の 3 点で
// `*(*(t[i]+j)+k)` 等が byte 加算 + 行のスカラ load になり SIGSEGV していた。
int g3[2][2][2];
int sum3(int (*p)[2][2], int n){
  int s = 0;
  for (int i=0;i<n;i++)
    for (int j=0;j<2;j++)
      for (int k=0;k<2;k++)
        s += *(*(p[i]+j)+k);          // 3D 行ポインタ算術 (param)
  return s;
}
int main(void){
  int t[2][2][2];
  for(int i=0;i<2;i++)for(int j=0;j<2;j++)for(int k=0;k<2;k++) t[i][j][k]=i*100+j*10+k;
  int r = 0;

  // 各種 3D 行算術 (ローカル)
  if (*(*(t[1]+1)+0) != 110) r |= 1;     // t[1][1][0]
  if (*(*(t[0]+1)+1) != 11)  r |= 2;     // t[0][1][1]
  if (*(*(t[1]+0)+0) != 100) r |= 4;     // t[1][0][0] (オフセット 0)
  if ((*(t[1]+1))[1] != 111) r |= 8;     // 中間行 decay + subscript
  if (*(t[1][0]+1) != 101)   r |= 16;    // 最内行 (int[2]) の算術

  // 通常の 3 重 subscript は不変
  int s = 0;
  for(int i=0;i<2;i++)for(int j=0;j<2;j++)for(int k=0;k<2;k++) s += t[i][j][k];
  if (s != 444) r |= 32;

  // 3D pointer-to-array 仮引数
  for(int i=0;i<2;i++)for(int j=0;j<2;j++)for(int k=0;k<2;k++) t[i][j][k]=1;
  if (sum3(t, 2) != 8) r |= 64;

  // global 3D の行算術
  for(int i=0;i<2;i++)for(int j=0;j<2;j++)for(int k=0;k<2;k++) g3[i][j][k]=i*4+j*2+k;
  if (*(*(g3[1]+1)+1) != 7) r |= 128;    // g3[1][1][1] = 4+2+1

  // 4D も中間行が連鎖的に decay する
  int q[2][2][2][2];
  for(int a=0;a<2;a++)for(int b=0;b<2;b++)for(int c=0;c<2;c++)for(int d=0;d<2;d++)
    q[a][b][c][d]=a*1000+b*100+c*10+d;
  if (*(*(*(q[1]+1)+1)+1) != 1111) r |= 256;

  return r == 0 ? 42 : r;
}
