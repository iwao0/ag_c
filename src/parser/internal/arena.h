#ifndef PARSER_ARENA_H
#define PARSER_ARENA_H

#include <stddef.h>

// ゼロクリア済みメモリを返すアリーナアロケータ
void *arena_alloc(size_t size);

// アリーナ全体を解放する（プログラム終了時）
void arena_free_all(void);

#endif
