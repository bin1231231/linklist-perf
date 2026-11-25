/*
 * variants.h - 用于对比的各个实现版本
 *
 * V0 baseline : 题目原始写法。每个节点、每份数据各 malloc 一次；FindNode 先比长度再 memcmp。
 * V1 pool     : 结构体和 FindNode 与 V0 完全相同，只改内存布局：
 *               同一条链表的节点从该链表自己的节点池中连续分配，数据放入公共数据区。
 * V2 compact  : 在 V1 基础上重构节点，把 next 指针和 8 字节“键”（长度 + 数据前 4 字节）
 *               放在节点开头，遍历时先比较键，键相同才访问数据。
 * V3 block    : 正式版本（linklist.h 中的 InitLink/AppendNode/FindNode）。
 *               节点结构体保持题目原样，节点按块连续存放，块内另存一个键数组；
 *               FindNode 按块从头遍历，用 AVX2 一条指令比较 4 个节点的键。
 */
#ifndef VARIANTS_H
#define VARIANTS_H

#include "linklist.h"

/* V0 */
LinkData *v0_InitLink(int size, char *mem);
void      v0_AppendNode(LinkData *firstNode, int size, char *mem);
LinkData *v0_FindNode(LinkData *firstNode, int size, char *mem);
void      v0_FreeLink(LinkData *firstNode);

/* V1 */
LinkData *v1_InitLink(int size, char *mem);
void      v1_AppendNode(LinkData *firstNode, int size, char *mem);
LinkData *v1_FindNode(LinkData *firstNode, int size, char *mem);
void      v1_FreeLink(LinkData *firstNode);

/* V2：重构后的节点（32 字节）。去掉 type，把数据前 4 字节存进节点，
 * dataSize 与 prefix 相邻，合起来正好是 8 字节的键，一次比较完成。 */
typedef struct LinkData2 {
    struct LinkData2 *next;     /* 遍历时用到的字段放在前 16 字节 */
    int  dataSize;
    uint32_t prefix;
    char *data;
    struct LinkData2 *prev;
} LinkData2;

LinkData2 *v2_InitLink(int size, char *mem);
void       v2_AppendNode(LinkData2 *firstNode, int size, char *mem);
LinkData2 *v2_FindNode(LinkData2 *firstNode, int size, char *mem);
void       v2_FreeLink(LinkData2 *firstNode);

/* V3 即正式接口 InitLink / AppendNode / FindNode / FreeLink */

#endif
