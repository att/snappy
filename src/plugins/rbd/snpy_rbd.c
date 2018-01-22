/*
 *  Copyright (c) 2016 AT&T Labs Research
 *  All rights reservered.
 *  
 *  Licensed under the GNU Lesser General Public License, version 2.1; you may
 *  not use this file except in compliance with the License. You may obtain a
 *  copy of the License at:
 *  
 *  https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 *  
 *
 *  Author: Pingkai Liu (pingkai@research.att.com)
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <rbd/librbd.h>



#include "json.h"
#include "snpy_util.h"
#include "snpy_blk_map.h"

struct rbd_data {
    rados_t cluster;
    rados_ioctx_t io_ctx;
    rbd_image_t image;
    rbd_image_info_t info;
}; 

#define RBD_CONF_SIZE 256

struct rbd_conf {
    char user[RBD_CONF_SIZE];
    char mon_host[RBD_CONF_SIZE];
    char key[RBD_CONF_SIZE];
    char pool[RBD_CONF_SIZE];
    char image[RBD_CONF_SIZE];
    char snap[RBD_CONF_SIZE];
};

struct rbd_hdr {
    u64 blk_dev_size;   /* total size */
    u64 blk_map_offset; /* location of block map */
    u64 compress_type;  /* type of compression */
};

#define SNPY_RBD_EBASE 0x10000


enum snpy_rbd_error {
    SNPY_RBD_ECONF = SNPY_RBD_EBASE,
    SNPY_RBD_ECONN,
    SNPY_RBD_EENV,
    SNPY_RBD_ESNAPCR,
    SNPY_RBD_ESTAT,
    SNPY_RBD_ELAST
};

static const char *snpy_rbd_errmsg_tab[] = {
    [SNPY_RBD_ECONF - SNPY_RBD_EBASE] = "error parsing rbd conf.",
    [SNPY_RBD_ECONN - SNPY_RBD_EBASE] = "fail connecting rbd.",
    [SNPY_RBD_EENV - SNPY_RBD_EBASE] = "job env error/incomplete.",
    [SNPY_RBD_ESNAPCR - SNPY_RBD_EBASE] = "can not create rbd snapshot.",
    [SNPY_RBD_ESTAT - SNPY_RBD_EBASE] = "can not get rbd image stat."
};

const char* snpy_rbd_strerror(int errnum) {

    if (errnum >= JSON_EBASE && errnum < JSON_ELAST) 
        return json_strerror(errnum);
    
    if (errnum >= SNPY_RBD_EBASE && errnum < SNPY_RBD_ELAST) 
        return snpy_rbd_errmsg_tab[errnum - SNPY_RBD_EBASE];
    else if (errnum < sys_nerr)
        return sys_errlist[errnum];
    else 
        return "unknown error";

}

int rbd_data_init(struct rbd_conf *conf, struct rbd_data *rbd) ;
static int diff_cb_snap(uint64_t off, size_t len, int exists, void *arg);
void rbd_data_destroy(struct rbd_data *rbd) ;

static int snpy_rbd_write_image(rbd_image_t image, u64 off, u64 len, int fd, 
                                char *buf, size_t buf_size) ;
static int do_snap(const char *arg, int arg_size);
static int do_export(const char *arg, int arg_size);
static int do_import(const char *arg, int arg_size);
static int do_diff(const char *arg, int arg_size);
static int do_patch(const char *arg, int arg_size);

static int diff_cb_snap(uint64_t off, size_t len, int exists, void *arg) {
    size_t *p = (size_t *)arg;
    if (exists) {
        (*p) += len;
    }
    return 1;
}

struct diff_cb_export_arg {
    int fd;
    rbd_image_t image;
    char *buf;
    struct blk_map *bm;
    int status;
};

static int diff_cb_export(uint64_t off, size_t len, int exists, void *arg) {
    struct diff_cb_export_arg *p = arg;
    int rc;
    if (!p || !p->buf || p->fd <= 0 || !p->image || !p->bm) {
        p->status = EINVAL;
        return -p->status;
    }
    if (exists) {
        /* update the segment list */
        rc = blk_map_add(&(p->bm), off, len);
        if (rc) {
            p->status = -rc;
            return rc;
        }

        /* write to data file */
        ssize_t nbyte = rbd_read(p->image, off, len, p->buf);
        if(nbyte != len) {
            p->status = -nbyte;
            return nbyte;
        }
        
        nbyte = write(p->fd, p->buf, len);
        if(nbyte != len) {
            p->status = errno;
            return -errno;
        }
        
    }
    return 0;
}

int rbd_conf_init(struct rbd_conf *conf, const char *arg) {
    if (!conf || !arg) 
        return -EINVAL;
    int error;
    struct json *js = json_open(JSON_F_NONE, &error);
    if (!js)
        return -error;
    int rc = json_loadstring(js, arg);
    if (rc) 
        goto close_js;
    
    strlcpy(conf->user, json_string(js, ".sp_param.user"), sizeof conf->user);
    strlcpy(conf->mon_host, json_string(js, ".sp_param.mon_host"), sizeof conf->mon_host);
    strlcpy(conf->key, json_string(js, ".sp_param.key"), sizeof conf->key);
    strlcpy(conf->pool, json_string(js, ".sp_param.pool"), sizeof conf->pool);
    strlcpy(conf->image, json_string(js, ".sp_param.image"), sizeof conf->image);
    strlcpy(conf->snap, json_string(js, ".sp_param.snap_name"), sizeof conf->snap);
    return 0;
close_js:
    return -SNPY_RBD_ECONF;
}


static ssize_t get_rbd_alloc_size(rbd_image_t image) {
    
    int rc = 0;
    rbd_image_info_t info;
    rc = rbd_stat(image, &info, sizeof info);
    if (rc)
        return rc;
    ssize_t alloc_size = 0;
    if ((rc = rbd_diff_iterate(image, NULL, 0, 
                               info.size, diff_cb_snap, &alloc_size))) {
        return rc;

    }   
    
    return alloc_size;

}

static int do_snap(const char *arg, int arg_size) {
    int rc;
    struct rbd_data rbd;
    struct rbd_conf conf;
    char str_buf[64];
    int status = 0;
    const char* status_msg = NULL;

    if ((rc = rbd_conf_init(&conf, arg))) {
        status = SNPY_RBD_ECONF;
        snpy_logger(SNPY_LOG_ERR, "error parsing rbd configuration: %d.", rc);
        goto err_out;
    }

    if((rc = rbd_data_init(&conf, &rbd))) {
        status = SNPY_RBD_ECONN;
        snpy_logger(SNPY_LOG_ERR, "error getting rbd connection: %d.", rc);
        goto err_out;
    }                                           /* allocation point: rbd */

    char job_id[32];
    if ((rc = kv_get_sval("meta/id", job_id, sizeof job_id, NULL))) {
        status = SNPY_RBD_EENV;
        snpy_logger(SNPY_LOG_ERR, "can not get meta/id: %d.", rc);
        goto cleanup_rbd_data;
    }

    time_t snap_start = time(NULL);
    char snap_name[64];
    sprintf(snap_name, "snpy-%s", job_id);
    if((rc = rbd_snap_create(rbd.image, snap_name))) {
        status = SNPY_RBD_ESNAPCR;
        snpy_logger(SNPY_LOG_ERR, "can not create image: %d.", rc);
        goto cleanup_rbd_data;
    }
    time_t snap_fin = time(NULL);

    if(rbd_snap_set(rbd.image, snap_name))  {
        status = -rc;
        goto cleanup_rbd_data;
    }

    if ((rc = rbd_stat(rbd.image, &rbd.info, sizeof rbd.info))) {
        status = SNPY_RBD_ESTAT;
        snpy_logger(SNPY_LOG_ERR, "can not get image stat: %d.", rc);
        goto cleanup_rbd_data;
    }
    ssize_t alloc_size = get_rbd_alloc_size(rbd.image);
    if (alloc_size < 0) 
        alloc_size = -1;
    /* update arg:
     * 1. set vol_size, alloc_size and snap_name in sp_param object
     * 2. set est_size in top level object.
     */
    int error;
    struct json *js = json_open(JSON_F_NONE, &error);
    if (!js) {
        status = error;
        goto cleanup_rbd_data;
    }                                                   /* allocation point: js */
    rc = json_loadstring(js, arg);
    if (rc) {
        status = rc;
        goto close_js;
    }

    if ((rc = json_setnumber(js, rbd.info.size, ".sp_param.vol_size")) ||
        (rc = json_setnumber(js, alloc_size, ".sp_param.alloc_size")) ||
        (rc = json_setnumber(js, snap_start, ".sp_param.snap_start")) ||
        (rc = json_setnumber(js, snap_fin, ".sp_param.snap_fin")) ||
        (rc = json_setstring(js, snap_name, ".sp_param.snap_name"))) {
        status = rc;
        goto close_js;
    } 
    
    FILE *arg_fp = fopen("meta/arg.out", "w");
    if (!arg_fp) {
        status = errno;
        goto close_js;
    }                                                   /* ALLOCATION POINT: arg_fp */
    
    if ((rc = json_printfile(js, arg_fp, 0))) {
        status = rc;
        goto fclose_arg_fp;
    }

fclose_arg_fp:
    fclose(arg_fp);

close_js:
    json_close(js);
 
cleanup_rbd_data:
    rbd_data_destroy(&rbd);
err_out:

    sprintf(str_buf, "%d", status);
    rc = kv_put_sval("meta/status", str_buf, sizeof str_buf, NULL); 
    status_msg = snpy_rbd_strerror(status);
    rc = kv_put_sval("meta/status_msg", status_msg, strlen(status_msg)+1, NULL);

    return rc;
}

static int update_export_arg(const char *arg, 
                             time_t export_start, 
                             time_t export_fin) {
    /* TODO: update arg:
     * 1. set vol_size, alloc_size and snap_name in sp_param object
     * 2. set est_size in top level object.
     */
    int rc;
    int status = 0;
    int error = 0;
    struct json *js = json_open(JSON_F_NONE, &error);
    if (!js) {
        status = error;
        goto err_out;
    }                                                   /* allocation point: js */
    rc = json_loadstring(js, arg);
    if (rc) {
        status = rc;
        goto close_js;
    }

    if ((rc = json_setnumber(js, export_start, ".sp_param.export_start")) ||
        (rc = json_setnumber(js, export_fin, ".sp_param.export_fin"))) {
        status = rc;
        goto close_js;
    } 
    
    FILE *arg_fp = fopen("meta/arg.out", "w");
    if (!arg_fp) {
        status = errno;
        goto close_js;
    }                                                   /* ALLOCATION POINT: arg_fp */
    
    if ((rc = json_printfile(js, arg_fp, 0))) {
        status = rc;
        goto fclose_arg_fp;
    }

fclose_arg_fp:
    fclose(arg_fp);

close_js:
    json_close(js);
err_out:    
    return status;
}


static int update_import_arg(const char *arg, 
                             time_t start, 
                             time_t fin) {
    /* update arg:
     * 1. set vol_size, alloc_size and snap_name in sp_param object
     * 2. set est_size in top level object.
     */
    int rc;
    int status = 0;
    int error = 0;
    struct json *js = json_open(JSON_F_NONE, &error);
    if (!js) {
        status = error;
        goto err_out;
    }                                                   /* allocation point: js */
    rc = json_loadstring(js, arg);
    if (rc) {
        status = rc;
        goto close_js;
    }

    if ((rc = json_setnumber(js, start, ".sp_param.import_start")) ||
        (rc = json_setnumber(js, fin, ".sp_param.import_fin"))) {
        status = rc;
        goto close_js;
    } 
    
    FILE *arg_fp = fopen("meta/arg.out", "w");
    if (!arg_fp) {
        status = errno;
        goto close_js;
    }                                                   /* ALLOCATION POINT: arg_fp */
    
    if ((rc = json_printfile(js, arg_fp, 0))) {
        status = rc;
        goto fclose_arg_fp;
    }

fclose_arg_fp:
    fclose(arg_fp);

close_js:
    json_close(js);
err_out:    
    return status;
}

static int do_export(const char *arg, int arg_size) {

    int rc;
    struct rbd_data rbd;
    struct rbd_conf conf;
    time_t start, fin;
    int status = 0;
    int status_msg[1024];
    char str_buf[64];
    

    /* prepare rbd image handle */
    start = time(NULL);
    if ((rc = rbd_conf_init(&conf, arg))) {
        status = EINVAL;
        goto err_out;
    }
    if((rc = rbd_data_init(&conf, &rbd))) {
        status = EINVAL;
        goto err_out;
    }
    char job_id[32];
    if ((rc = kv_get_sval("meta/id", job_id, sizeof job_id, NULL))) {
        snpy_logger(SNPY_LOG_ERR, "missing meta/id file: %d", rc);
        status = -rc;
        goto cleanup_rbd_data;
    }
    if((rc = rbd_stat(rbd.image, &rbd.info, sizeof rbd.info))) {
        snpy_logger(SNPY_LOG_ERR, "error get rbd image info: %d", rc);
        status = -rc;
        goto cleanup_rbd_data;
    }

    rc = rbd_snap_set(rbd.image, conf.snap);
    if (rc) {
        snpy_logger(SNPY_LOG_ERR, 
                    "error set rbd image context to the snapshot: %d", rc);
        status = -rc;
        goto cleanup_rbd_data;
    }   /* done prepare rbd image */

    /* prepare export call back function write buffer */
    char *buf = malloc(rbd.info.obj_size);
    if (!buf) {
        snpy_logger(SNPY_LOG_ERR, "can not calloc write call-back function buffer");
        status = errno;
        goto cleanup_rbd_data;
    }

    /* prepare data file for write */
    char data_fn[PATH_MAX]="data/";
    /* use job id as data file name */
    if (strlcat(data_fn, job_id, PATH_MAX) >= PATH_MAX) {
        status = ENAMETOOLONG;
        goto free_buf;
    }
    int data_fd = open(data_fn, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (data_fd == -1) {
        status = errno;
        goto free_buf;
    }

    struct rbd_hdr hdr = {
        .blk_dev_size = rbd.info.size,
        .blk_map_offset = -1,
        .compress_type = 1
    };
    /* seek pass the rbd header */
    lseek(data_fd, sizeof(struct rbd_hdr), SEEK_SET);
    
    /* prepare export data block map */
    /* initialize callback argument */
    struct diff_cb_export_arg export_arg =
    {   .image = rbd.image,
        .fd = data_fd,
        .buf = buf,
        .bm = blk_map_alloc(4096),
        .status = 0
    };

    /* check blk_mapp_alloc return */
    if (!export_arg.bm) {
        status = ENOMEM;
        snpy_logger(SNPY_LOG_ERR, "can not alloc export data block map");
        goto close_data_fd;
    }


    rc = rbd_diff_iterate(rbd.image, NULL, 0, rbd.info.size,
                          diff_cb_export, &export_arg);

    if (rc)  {
        status = -rc;
        snpy_logger(SNPY_LOG_ERR, "rbd_diff_iterate: %d.", rc);
        goto free_blk_map;
    } 

    if (export_arg.status) {
        status = export_arg.status;
        snpy_logger(SNPY_LOG_ERR, "rbd_diff_iterate: %d.", rc);
        goto free_blk_map;
    }

    /* finishing export task  */
    
    hdr.blk_map_offset = lseek(data_fd, 0, SEEK_CUR); /* save current offset */

    rc = blk_map_write(data_fd, export_arg.bm);    /* write block_map */
    if (rc == -1) {
        status = errno;
        goto free_blk_map;
    }

    /* append the data tag */
    char tag_buf[4096];
    int tag_fd = open("meta/tag", O_RDONLY);
    if (tag_fd < 0) {
        status = errno;
        snpy_logger(SNPY_LOG_ERR, "can not open tag file: %d.", errno);
        goto free_blk_map;
    }
    ssize_t nread = read(tag_fd, tag_buf, sizeof tag_buf);
    if (nread != sizeof tag_buf) {
        status = errno;     /* save errno */
        snpy_logger(SNPY_LOG_ERR, "error read tag file: %d.", status);
        close(tag_fd);
        goto free_blk_map;
    }
    close(tag_fd);
    ssize_t nwrite = write(data_fd, tag_buf, sizeof tag_buf);
    if (nwrite != sizeof tag_buf) {
        snpy_logger(SNPY_LOG_ERR, "error append tag file: %d.", errno);
        goto free_blk_map;
    }
   
    /* fill in rbd_hdr */
    lseek(data_fd, 0, SEEK_SET);
    if ((nwrite = write(data_fd, &hdr, sizeof hdr)) != sizeof hdr) {
        snpy_logger(SNPY_LOG_ERR, "error update rbd data header: %d", errno);
        goto free_blk_map;
    }


    fin = time(NULL);
    if ((rc = update_export_arg(arg, start, fin))) {
        snpy_logger(SNPY_LOG_ERR, "update_export_arg: %d.", rc);
    }

free_blk_map:
    blk_map_free(export_arg.bm);
close_data_fd:
    close(data_fd);
free_buf:
    free(buf);
cleanup_rbd_data:
    rbd_data_destroy(&rbd);
err_out:
    kv_put_ival("meta/status", status, NULL);
    strerror_r(status, str_buf, sizeof str_buf);
    kv_put_sval("meta/status_msg", str_buf, sizeof str_buf, NULL);

    return -status;

}



static int snpy_rbd_write_image(rbd_image_t image, u64 off, u64 len, int fd, 
                         char *buf, size_t buf_size) {

    ssize_t nread, nwrite;
    while (1) {
        if (len <= buf_size) {
            nread = read(fd, buf, len);
            if (nread != len) 
                return -errno;

            nwrite = rbd_write(image, off, len, buf);
            if (nwrite != len) 
                return -errno;

            break;
        } else {
            nread = read(fd, buf, buf_size); 
            if (nread != buf_size)
                return -errno;
            nwrite = rbd_write(image, off, buf_size, buf);
            if (nwrite != buf_size)
                return -errno;

            off += buf_size;
            len -= buf_size;
        }
    }
    return 0;
}

static int do_import(const char *arg, int arg_size) {
    int rc;
    struct rbd_data rbd;
    struct rbd_conf conf;
    time_t start, fin;
    int status = 0;
    char status_msg[1024] = "";
    char str_buf[64];
    
    start = time(NULL);
    /* prepare rbd connection */
    if ((rc = rbd_conf_init(&conf, arg))) {
        status = -rc;
        snprintf(status_msg, sizeof status_msg,
                 "configuration invalid: %d\n", status);
        goto err_out;
    }
    if((rc = rbd_data_init(&conf, &rbd))) {
        status = -rc;
        snprintf(status_msg, sizeof status_msg,
                 "error initiating rbd connection: %d\n", status);
        goto err_out;
    }                                       /* RAII point */
    char job_id[32];
    if (kv_get_sval("meta/id", job_id, sizeof job_id, NULL)) {
        status = -rc;
        snprintf(status_msg, sizeof status_msg,
                 "error getting job id: %d\n", status);
        goto cleanup_rbd_data;
    }
    /*
    char rstr_job_id[32];
    if (kv_get_sval("meta/rstr_job_id", rstr_job_id, sizeof rstr_job_id, NULL)) {
        status = -rc;
        goto cleanup_rbd_data;
    }
    */
    if((rc = rbd_stat(rbd.image, &rbd.info, sizeof rbd.info))) {
        status = -rc;
        snprintf(status_msg, sizeof status_msg,
                 "error getting rbd stat: %d\n", status);
        goto cleanup_rbd_data;
    }
    
    /* allocation buffer */
    size_t buf_size = 4*(1<<20);
    char *buf = malloc(buf_size);
    if (!buf) {
        status = ENOMEM;
        snprintf(status_msg, sizeof status_msg,
                 "error allocating import buffer: %d\n", status);
        goto cleanup_rbd_data;
    }                                       /* RAII point */

    char data_fn[PATH_MAX]="data/data";
    int data_fd = open(data_fn, O_RDONLY, 0600);
    if (data_fd == -1) {
        status = errno;
        snprintf(status_msg, sizeof status_msg,
                 "error open data file: %d\n", status);
        goto free_buf;
    }                                       /* RAII point */

    struct rbd_hdr hdr;
    ssize_t nread = pread(data_fd, &hdr, sizeof hdr, 0);
    if ( nread != sizeof hdr) {
        status = errno;
        snprintf(status_msg, sizeof status_msg,
                 "error read rbd header: %d\n", status);
        goto close_data_fd;
    }

    /* read out block map */
    off_t offset = lseek(data_fd, hdr.blk_map_offset, SEEK_SET);
    struct blk_map *bm;
    if ((rc = blk_map_read(data_fd, &bm))) {
        status = -rc;
        snprintf(status_msg, sizeof status_msg,
                 "error read block map: %d.", status);
        goto close_data_fd;
    }                                       /* RAII point */
    /* TODO: sanity check for blk_map */
    
    /* EXPRIMENTAL: do discard */
    /*
    snpy_logger(SNPY_LOG_DEBUG, "starting discarding rbd volume: %d.");
    rc = rbd_discard(rbd.image, 0, rbd.info.size);
    snpy_logger(SNPY_LOG_DEBUG, "done discarding rbd volume: %d.", rc);
    */
    /* writing rbd image */
    lseek(data_fd, sizeof hdr, SEEK_SET); /* seek to the beginning of data */
    int i;
    for (i = 0; i < bm->nuse; i ++) {
        u64 off = bm->segv[i].off;
        u64 len = bm->segv[i].len;
        if ((rc = snpy_rbd_write_image(rbd.image, off, len, data_fd,
                                       buf, buf_size)))
        {
            status = -rc;
            snprintf(status_msg, sizeof status_msg,
                     "error write image: %d.", status);
            goto free_bm;
        }
        snpy_logger(SNPY_LOG_DEBUG, "done writing segment: %d.", i);
    }
    
    fin = time(NULL);
    /* update arg: add import starting and finishing time */
    if ((rc = update_import_arg(arg, start, fin))) {
        status = -rc;
        snprintf(status_msg, sizeof status_msg,
                 "update_import_arg: %d.", status);
    }
free_bm:
    blk_map_free(bm);
close_data_fd:
    close(data_fd);
free_buf:
    free(buf);
cleanup_rbd_data:
    rbd_data_destroy(&rbd);
err_out:
    kv_put_ival("meta/status", status, NULL);
    kv_put_sval("meta/status_msg", status_msg, sizeof status_msg, NULL);

    snpy_logger(SNPY_LOG_ERR, status_msg);
    return -status;

}

static int do_diff(const char *arg, int arg_size) {
    return 0;
}

static int do_patch(const char *arg, int arg_size) {
    return 0;
}

int rbd_data_init(struct rbd_conf *conf, struct rbd_data *rbd) {
    int rc;
    if (!conf ||  !rbd)
        return -EINVAL;
    if ((rc = rados_create(&rbd->cluster, conf->user))) 
        goto err_out;
    if ((rc = rados_conf_set(rbd->cluster, "mon_host", conf->mon_host)) ||
       (rc = rados_conf_set(rbd->cluster, "key", conf->key)))
        goto free_rados_cluster;

    if ((rc = rados_connect(rbd->cluster))) 
        goto free_rados_cluster;
    if ((rc = rados_ioctx_create(rbd->cluster, conf->pool, &rbd->io_ctx))) 
        goto free_rados_cluster;
    if ((rc = rbd_open(rbd->io_ctx, conf->image, &rbd->image, NULL))) {
        snpy_logger(SNPY_LOG_ERR, "rbd_data_init: can not open rbd vol: %d", rc);
        goto free_rados_ioctx;
    }
    return 0;

free_rados_ioctx:
    rados_ioctx_destroy(rbd->io_ctx);
    rbd->io_ctx = NULL;

free_rados_cluster:
    rados_shutdown(rbd->cluster);
    rbd->cluster = NULL;
err_out:
    return rc;
}

void rbd_data_destroy(struct rbd_data *rbd) {
    rbd_close(rbd->image);
    rados_ioctx_destroy(rbd->io_ctx);
    rados_shutdown(rbd->cluster);
}

int main(void) {
    int rc;
    char cmd[32];
    char arg[4096];
    char id_buf[64];
    int job_id;

    /* open log */
    snpy_logger_open("meta/log", 0);
    

    if ((rc = kv_get_sval("meta/cmd", cmd, sizeof cmd, NULL))) 
        goto err_out;
    if ((rc = kv_get_sval("meta/id", id_buf, sizeof id_buf, NULL)))
        goto err_out;
    job_id = atoi(id_buf);

    if (kv_get_sval("meta/arg", arg, sizeof arg, NULL))
        goto err_out;
    char buf[64];
    struct rbd_conf conf;
    if (!strcmp(cmd, "snap")) {
        rc = do_snap(arg, sizeof arg);
    } else if (!strcmp(cmd, "export")) {
        rc = do_export(arg, sizeof arg);
    } else if (!strcmp(cmd, "diff")) {
        rc = do_diff(arg, sizeof arg);
    } else if (!strcmp(cmd, "import")) {
        rc = do_import(arg, sizeof arg);
    } else if (!strcmp(cmd, "patch")) {
        rc = do_patch(arg, sizeof arg);
    } 
err_out:
    snpy_logger_close(0);
    if (!rc) 
        return job_id;
    else 
        return rc;
}
