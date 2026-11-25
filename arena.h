/*
 * arena.h - 简单的顺序分配器，用于存放节点数据（data 指向的内容）。
 * 数据只在键匹配后才被访问，属于“冷”数据，集中存放即可。
 */
#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

void *arena_alloc(size_t n);
char *arena_copy(const char *src, int n);
void  arena_reset(void);          /* 释放全部已分配内存 */
size_t arena_bytes(void);         /* 当前占用字节数 */

#endif
