#!/bin/bash

# テスト関数の定義: assert [期待する終了ステータス] [入力文字列]
assert() {
  expected="$1"
  input="$2"

  # コンパイラを実行し、アセンブリファイルを生成する
  ./build/ag_c "$input" > build/tmp.s
  
  # エラーがあればテスト失敗
  if [ $? -ne 0 ]; then
    echo "Fail: Failed to compile $input"
    exit 1
  fi

  # 生成されたアセンブリをclangでアセンブルし、実行ファイルを生成
  clang -o build/tmp build/tmp.s
  
  # エラーがあればテスト失敗
  if [ $? -ne 0 ]; then
    echo "Fail: Failed to assemble build/tmp.s"
    exit 1
  fi

  # 生成した実行ファイルを実行
  ./build/tmp
  actual="$?"

  # 結果の検証
  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

# テストケース
# 単一の整数のテスト
assert 0 "0"
assert 42 "42"

# 加減算のテスト
assert 21 "5+20-4"
assert 41 " 12 + 34 - 5 "

# 四則演算と括弧のテスト
assert 47 "5+6*7"
assert 15 "5*(9-6)"
assert 4 "(3+5)/2"

echo OK
