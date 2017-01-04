#include "snpy_data_tag.h"

#include <string.h>

void snpy_data_tag_init(struct snpy_data_tag *buf) {
    if (!buf)
        return;

    memset(buf, 0, SNPY_DATA_TAG_SIZE);
    buf->magic = SNPY_DATA_TAG_MAGIC;

    return;

}


