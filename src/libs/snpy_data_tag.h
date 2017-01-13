#ifndef SNPY_DATA_TAG_H
#define SNPY_DATA_TAG_H

#define SNPY_DATA_TAG_SIZE 4096
#define SNPY_DATA_TAG_MAGIC 0x79706e73

#include "snpy_util.h"

/*
 * The snappy backup data are sent to target in the form of packets.
 * 
 * all the integer type fields are in little-endian byte order.
 */

struct snpy_data_tag {
    u32 magic;                  /* 0x79706e73 */
    u32 dep_id;                 /* directly dependent job */
    u32 job_id;                 /* job id */
    u32 frag_id;                /* fragmented job id */
    u64 snap_ts;                /* unix timestamp of snapshot */
    u16 src_plugin_id;          /* source plugin id */
    u16 src_plugin_ver;         /* source plugin version */
    u16 tgt_plugin_id;          /* target plugin id */
    u16 tgt_plugin_ver;         /* target plugin code */
    union {
        u32 flags;                  /* flags: */
        struct {
            u8 is_frag:1;       /* fragmented or not */
            u8 frag_mode:1;     /* sequential or union */
            u8 is_blk:1;        /* block device backup? */
            u8 is_fs:1;         /* file system backup? */
            u8 is_obj:1;        /* object backup *? */
        };
    };
    u8 reserved[4];             /* reservered */
    /* packet specific fields */
    u64 pkt_off;                /* packet off set */
    u64 pkt_len;                /* packet length */
    /* check sum field */
    u8  chksum[16];             /* md5 checksum */
    u8 pkt_chksum[16];          /* checksum for current packet */       
    u32 chk_nday;               /* days of last check since snapshot */
    u32 pkt_chk_nday;           /* time diff between last check and snapshot */
    u8  extra_data[4000];       /* fill to 4096 */
};

void snpy_data_tag_init(struct snpy_data_tag *buf);

#endif
