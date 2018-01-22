#ifndef SNPY_BLK_MAP_H
#define SNPY_BLK_MAP_H

#include "snpy_util.h"

struct seg {
    u64 off;
    u64 len;
};


struct blk_map {
    u64 nalloc;
    u64 nuse;
    struct seg segv[0];
};


struct blk_map *blk_map_alloc(size_t n);
int blk_map_add(struct blk_map **bm, u64 off, u64 len);
void blk_map_free(struct blk_map *bm);

int blk_map_write(int fd, struct blk_map *bm);
int blk_map_read(int fd, struct blk_map **bm);
#endif
