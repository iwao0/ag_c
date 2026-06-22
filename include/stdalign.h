#ifndef _STDALIGN_H
#define _STDALIGN_H
/* C11 7.15: alignas/alignof を _Alignas/_Alignof のマクロにする。 */
#define alignas _Alignas
#define alignof _Alignof
#define __alignas_is_defined 1
#define __alignof_is_defined 1
#endif
