/*
 * linklist.h - 双向链表搜索：公共定义
 *
 * 题目要求的结构体与三个接口（InitLink / AppendNode / FindNode）。
 * 正式接口由优化版本 V3（分块链表 + SIMD 比较）实现，见 link_v3_block.c。
 * 为了对比优化效果，另外保留了 V0~V2 三个版本，接口加前缀区分，见 variants.h。
 */
#ifndef LINKLIST_H
#define LINKLIST_H

#include <stdint.h>

typedef struct LinkData {
    struct LinkData *prev;
    struct LinkData *next;
    int  type;
    int  dataSize;
    char *data;
} LinkData;

#define LINK_TYPE_HEAD 1
#define LINK_TYPE_DATA 0

/* 创建链表头并返回；头节点的 prev、next 指向自己 */
LinkData *InitLink(int size, char *mem);

/* 追加到尾部；firstNode->prev 始终指向最后一个节点 */
void AppendNode(LinkData *firstNode, int size, char *mem);

/* 从头遍历，返回第一个数据相同的节点地址，找不到返回 NULL */
LinkData *FindNode(LinkData *firstNode, int size, char *mem);

/* 释放整条链表（题目未要求，测试程序使用） */
void FreeLink(LinkData *firstNode);

/*
 * 节点“键”：低 32 位是数据长度，高 32 位是数据的前 4 个字节（不足 4 字节补 0）。
 * 这是数据本身的直接拷贝，不是哈希值：两个节点的键不同，数据一定不同；
 * 键相同时仍需比较完整数据。用它一次比较就能同时筛掉长度不同和开头不同的节点。
 */
static inline uint64_t link_make_key(int size, const char *mem)
{
    uint32_t prefix = 0;
    int n = size < 4 ? size : 4;
    for (int i = 0; i < n; i++)
        prefix |= (uint32_t)(unsigned char)mem[i] << (8 * i);
    return (uint64_t)(uint32_t)size | ((uint64_t)prefix << 32);
}

#endif
