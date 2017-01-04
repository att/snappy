#include <stdlib.h>
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



int blk_map_add(struct blk_map *bm, u64 off, u64 len) {
   if (!bm || !bm->nalloc || (bm->nuse > bm->nalloc))
       return -EINVAL;


   /* add initial segment */
   if (bm->nuse == 0) {
       bm->segv[0].off = off;
       bm->segv[0].len = len;
       bm->nuse = 1;
       return 0;
   } 
   
   /* can it be merged into existing segment?  */
   if (bm->segv[bm->nuse - 1].off + bm->segv[bm->nuse - 1].len == off) {
       bm->segv[bm->nuse - 1].len += len;
   } else {
       if (bm->nuse == bm->nalloc) {    /* resize needed */
            void *p = realloc(bm, bm->nalloc << 1);
            if (!p) 
                return -ENOMEM;
            bm = p; 
            bm->nalloc <<= 1;
       }
       bm->segv[bm->nuse].off = off;
       bm->segv[bm->nuse].len = len;
       bm->nuse ++;
   }

   return 0;
}

void blk_map_free(struct blk_map *bm) {
    free(bm);
    return;
}
