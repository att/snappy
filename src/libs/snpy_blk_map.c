#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "snpy_blk_map.h"


struct blk_map *blk_map_alloc(size_t n) {
    if (n <= 0) 
        n = 1024;
    struct blk_map *p  = calloc(sizeof(*p) + (sizeof p->segv[0]) * n, 1);
    if (!p) 
        return p;

    p->nalloc = n;
    return p;
}



int blk_map_add(struct blk_map **bm, u64 off, u64 len) {
    if (!bm || (!*bm) || !(*bm)->nalloc || ((*bm)->nuse > (*bm)->nalloc))
        return -EINVAL;


    /* add initial segment */
    if ((*bm)->nuse == 0) {
        (*bm)->segv[0].off = off;
        (*bm)->segv[0].len = len;
        (*bm)->nuse = 1;
        return 0;
    } 

    /* can it be merged into existing segment?  */
    if ((*bm)->segv[(*bm)->nuse - 1].off + (*bm)->segv[(*bm)->nuse - 1].len == off) {
        (*bm)->segv[(*bm)->nuse - 1].len += len;  /* merge the segment */
    } else {
        if ((*bm)->nuse == (*bm)->nalloc) {    /* resize needed */
            size_t new_size = 
                sizeof(**bm) + ((*bm)->nalloc << 1)  * (sizeof (*bm)->segv[0]);

            void *p = realloc((*bm), new_size);
            if (!p) 
                return -ENOMEM;
            (*bm) = p;
            (*bm)->nalloc <<= 1;
        }
        (*bm)->segv[(*bm)->nuse].off = off;
        (*bm)->segv[(*bm)->nuse].len = len;
        (*bm)->nuse ++;
    }
    return 0;
}


/* blk_map_write() - write block map to a give fd
 *
 * Note: it will change the offset of the @fd
 */

int blk_map_write(int fd, struct blk_map *bm) {
    if (!bm || fd < 0)
        return -EINVAL;

    size_t size = (sizeof bm->nuse) + bm->nuse * (sizeof bm->segv[0]);
    ssize_t nwrite = write(fd, &(bm->nuse), size);
    if (nwrite != size) 
        return -errno;

    return 0;
}

/* blk_map_read() - read a blk_map from an open fd
 *
 */

int blk_map_read(int fd, struct blk_map **bm) {
    if (!bm || fd < 0) 
        return -EINVAL;

    u64 nseg = 0;
    if (read(fd, &nseg, sizeof nseg) != sizeof nseg) 
        return -errno;
    struct blk_map *p = blk_map_alloc(nseg);
    if (!p) 
        return -ENOMEM;
    p->nuse = nseg;
    size_t size = nseg * (sizeof p->segv[0]);
    ssize_t nread = read(fd, p->segv, size);
    if (nread != size) {
        blk_map_free(p);
        return -errno;
    }
    *bm = p;
    return 0;

}


void blk_map_free(struct blk_map *bm) {
    free(bm);
    return;
}


