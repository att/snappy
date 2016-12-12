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

#include <rbd/librbd.h>
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
#include "json.h"
#include "util.h"

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

struct seg {
    u64 offset;
    u64 len;
};

struct data_tag {
    union {
        struct {
            u32 job_id;
            u64 time;
            u64 size;
        };
        u8 hdr[256];
    };
    /* rbd specific */
    union {
        struct {
            u32 segc;
            struct seg segv[239];
        };
        u8 rbd[4096-256];
    };
};




const char* snpy_rbd_strerror(int e) {

    return json_strerror(e);
}

int rbd_data_init(struct rbd_conf *conf, struct rbd_data *rbd) ;
static int diff_cb_snap(uint64_t off, size_t len, int exists, void *arg);
void rbd_data_destroy(struct rbd_data *rbd) ;

static int kv_get_val(const char *key, char *val, int val_size);
static int kv_put_val(const char *key, const char *val, int val_size);

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
    struct data_tag *tag;
    int status;
};

static int diff_cb_export(uint64_t off, size_t len, int exists, void *arg) {
    struct diff_cb_export_arg *p = arg;
    int rc;
    if (!p || !p->buf || p->fd <= 0 || !p->image) {
        p->status = EINVAL;
        return -p->status;
    }
    if (exists) {
        /* update the segment list */
        int segc = p->tag->segc; 
        int last = segc - 1;
        struct seg *segv = p->tag->segv;
        if (segc == 0) {
            segv[0].offset = off;
            segv[0].len = len;
            p->tag->segc ++;
        } else if (segv[last].offset + segv[last].len == off) {
            segv[last].len += len;
        } else {
            if (last + 1 >= ARRAY_SIZE(p->tag->segv)) 
                return -ERANGE;
            segv[last+1].offset = off;
            segv[last+1].len = len;
            p->tag->segc ++;
        }
        /* write to data file */
        ssize_t nbyte;
        nbyte = rbd_read(p->image, off, len, p->buf);
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

int kv_get_val(const char *key, char *val, int val_size) {
    int fd;
    int status = 0;
    struct stat sb;

    if ((fd = open(key, O_RDONLY)) == -1) 
        return -errno;
    if (fstat(fd, &sb)) {
        status = errno;
        goto close_fd;
    }
    if (sb.st_size >= val_size) {
        status = ERANGE;
        goto close_fd;
    }

    int nbyte = read(fd, val, sb.st_size); 
    if (nbyte != sb.st_size) {
        status = errno;
        goto close_fd;
    }
    val[nbyte] = 0;
//    char *p; (p = strchr(val, '\n')) &&  (*p = 0);
    return 0;  

close_fd:
    close(fd);
    return -status;
}

int kv_put_val(const char *key, const char *val, int val_size) {
    int val_len = strnlen(val, val_size);
    int fd;
    if (val_len >= val_size)
        goto err_out;
    if ((fd = open(key, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0)
        goto err_out;
    if (write(fd, val, val_len) != val_len)
        goto close_fd;
    return 0;
close_fd:
        close(fd);
err_out:
        return 1;
}

int rbd_conf_init(struct rbd_conf *conf, const char *arg) {
    if (!conf || !arg) 
        return -EINVAL;
    int error;
    struct json *js = json_open(JSON_F_NONE, &error);
    if (!js)
        return -error;
    int rc = json_loadstring(js, arg);
    if (rc) {
        goto close_js;
        return rc;
    }
    strlcpy(conf->user, json_string(js, ".sp_param.user"), sizeof conf->user);
    strlcpy(conf->mon_host, json_string(js, ".sp_param.mon_host"), sizeof conf->mon_host);
    strlcpy(conf->key, json_string(js, ".sp_param.key"), sizeof conf->key);
    strlcpy(conf->pool, json_string(js, ".sp_param.pool"), sizeof conf->pool);
    strlcpy(conf->image, json_string(js, ".sp_param.image"), sizeof conf->image);
    strlcpy(conf->snap, json_string(js, ".sp_param.snap_name"), sizeof conf->snap);

close_js:
    json_close(js);
    return rc;
}



static int do_snap(const char *arg, int arg_size) {
    int rc;
    struct rbd_data rbd;
    struct rbd_conf conf;
    char str_buf[64];
    int status = 0;
    const char* status_msg = NULL;

    if ((rc = rbd_conf_init(&conf, arg))) {
        status = -rc;
        goto err_out;
    }
    if((rc = rbd_data_init(&conf, &rbd))) {
        status = -rc;
        goto err_out;
    }                                           /* allocation point: rbd */
    char job_id[32];
    if ((rc = kv_get_val("meta/id", job_id, sizeof job_id))) {
        status = -rc;
        goto cleanup_rbd_data;
    }
    time_t snap_start = time(NULL);
    char snap_name[64];
    sprintf(snap_name, "snpy-%s", job_id);
    if((rc = rbd_snap_create(rbd.image, snap_name))) {
        status = -rc;
        goto cleanup_rbd_data;
    }
    if(rbd_snap_set(rbd.image, snap_name))  {
        status = -rc;
        goto cleanup_rbd_data;
    }
    time_t snap_fin = time(NULL);
    rc = rbd_stat(rbd.image, &rbd.info, sizeof rbd.info);

    size_t alloc_size = 0;
    size_t est_size = 0;
    if ((rc = rbd_diff_iterate(rbd.image, NULL, 0, 
                               rbd.info.size, diff_cb_snap, &alloc_size))) {
        status = -rc;
        goto cleanup_rbd_data;              
    }   
    est_size = alloc_size;

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
        (rc = json_setstring(js, snap_name, ".sp_param.snap_name")) ||
        (rc = json_setnumber(js, est_size, ".sp_param.est_size"))) {
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
    rc = kv_put_val("meta/status", str_buf,sizeof str_buf); 
    status_msg = snpy_rbd_strerror(status);
    rc = kv_put_val("meta/status_msg", status_msg, strlen(status_msg)+1);

    return rc;
}

static int update_export_arg(const char *arg, 
                             time_t export_start, 
                             time_t export_fin) {
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


static int do_export(const char *arg, int arg_size) {
    int rc;
    struct rbd_data rbd;
    struct rbd_conf conf;
    time_t start, fin;
    int status = 0;
    int status_msg[1024];
    char str_buf[64];
    
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
    if (kv_get_val("meta/id", job_id, sizeof job_id)) {
        status = -rc;
        goto cleanup_rbd_data;
    }
    if((rc = rbd_stat(rbd.image, &rbd.info, sizeof rbd.info))) {
        status = -rc;
        goto cleanup_rbd_data;
    }
    char *buf = malloc(rbd.info.obj_size);
    if (!buf) {
        status = ENOMEM;
        goto cleanup_rbd_data;
    }
    char data_fn[PATH_MAX]="data/";
    int data_fd = open(strncat(data_fn, job_id, ARRAY_SIZE(data_fn)), 
                       O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (data_fd == -1) {
        status = errno;
        goto free_buf;
    }
    
    struct data_tag tag = 
    {   
        .time = time(NULL),
        .segc = 0,
        .segv = {{0}}
    };
    struct diff_cb_export_arg export_arg =
    {   .image = rbd.image,
        .fd = data_fd,
        .buf = buf,
        .tag = &tag,
        .status = 0
    };
    rc = rbd_snap_set(rbd.image, conf.snap);
    if (rc) {
        status = -rc;
        goto close_data_fd;
    }
    rc = rbd_diff_iterate(rbd.image, NULL, 0, rbd.info.size, diff_cb_export, &export_arg);
    if (rc)  {
        status = -rc;
        goto close_data_fd;
    } 
    if (export_arg.status) {
        status = export_arg.status;
        goto close_data_fd;
    }
    /* write the tag */
    rc = write(data_fd, &tag, sizeof tag);
    if (rc == -1) {
        status = errno;
        goto close_data_fd;
    }
    
    fin = time(NULL);
    update_export_arg(arg, start, fin);
close_data_fd:
    close(data_fd);
free_buf:
    free(buf);
cleanup_rbd_data:
    rbd_data_destroy(&rbd);
err_out:
    sprintf(str_buf, "%d", status);
    fprintf(stderr, "export error: %s.\n", str_buf);
    kv_put_val("meta/status", str_buf,sizeof str_buf);
    strerror_r(status, str_buf, sizeof str_buf);
    kv_put_val("meta/status_msg", str_buf, sizeof str_buf);

    return -status;

}


static int do_import(const char *arg, int arg_size) {
    int rc;
    struct rbd_data rbd;
    struct rbd_conf conf;
    time_t start, fin;
    int status = 0;
    int status_msg[1024];
    char str_buf[64];
    
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
    if (kv_get_val("meta/id", job_id, sizeof job_id)) {
        status = -rc;
        goto cleanup_rbd_data;
    }
    if((rc = rbd_stat(rbd.image, &rbd.info, sizeof rbd.info))) {
        status = -rc;
        goto cleanup_rbd_data;
    }
    char *buf = malloc(rbd.info.obj_size);
    if (!buf) {
        status = ENOMEM;
        goto cleanup_rbd_data;
    }
    char data_fn[PATH_MAX]="data/";
    int data_fd = open(strncat(data_fn, job_id, ARRAY_SIZE(data_fn)), 
                       O_WRONLY|O_CREAT|O_TRUNC, 0600);
    if (data_fd == -1) {
        status = errno;
        goto free_buf;
    }
    
    struct data_tag tag = 
    {   
        .time = time(NULL),
        .segc = 0,
        .segv = {{0}}
    };
    struct diff_cb_export_arg export_arg =
    {   .image = rbd.image,
        .fd = data_fd,
        .buf = buf,
        .tag = &tag,
        .status = 0
    };
        
    rc = rbd_diff_iterate(rbd.image, NULL, 0, rbd.info.size, diff_cb_export, &export_arg);
    if (rc)  {
        status = -rc;
        goto close_data_fd;
    } 
    if (export_arg.status) {
        status = export_arg.status;
        goto close_data_fd;
    }
    /* write the tag */
    rc = write(data_fd, &tag, sizeof tag);
    if (rc == -1) {
        status = errno;
        goto close_data_fd;
    }
    
    fin = time(NULL);
    update_export_arg(arg, start, fin);
close_data_fd:
    close(data_fd);
free_buf:
    free(buf);
cleanup_rbd_data:
    rbd_data_destroy(&rbd);
err_out:
    sprintf(str_buf, "%d", status);
    kv_put_val("meta/status", str_buf,sizeof str_buf);
    strerror_r(status, str_buf, sizeof str_buf);
    kv_put_val("meta/status_msg", str_buf, sizeof str_buf);

    return -status;


    return 0;
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
        goto err_out;

    if (rados_create(&rbd->cluster, conf->user)) 
        goto err_out;

    if (rados_conf_set(rbd->cluster, "mon_host", conf->mon_host) ||
        rados_conf_set(rbd->cluster, "key", conf->key))
        goto free_rados_cluster;
    if (rados_connect(rbd->cluster)) 
        goto free_rados_cluster;
    if (rados_ioctx_create(rbd->cluster, conf->pool, &rbd->io_ctx)) 
        goto free_rados_cluster;
    if ((rc = rbd_open(rbd->io_ctx, conf->image, &rbd->image, NULL))) {
        printf("%s\n", strerror(-rc));
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
    return 1;
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
    if (kv_get_val("meta/cmd", cmd, sizeof cmd)) 
        goto err_out;
    if (kv_get_val("meta/id", id_buf, sizeof id_buf))
        goto err_out;
    job_id = atoi(id_buf);

    if (kv_get_val("meta/arg", arg, sizeof arg))
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

    return job_id;
}
