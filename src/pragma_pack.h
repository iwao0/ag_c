#ifndef PRAGMA_PACK_H
#define PRAGMA_PACK_H

// #pragma pack の現在のアライメント値（0 = 自然アライメント）
extern int pragma_pack_current;

// プリプロセッサから呼ばれる操作
void pragma_pack_push(int alignment);
void pragma_pack_pop(void);
void pragma_pack_set(int alignment);
void pragma_pack_reset(void);

#endif
