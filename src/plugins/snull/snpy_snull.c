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



#include "snpy_util.h"
#include "json.h"


/* define error  type */
#define SNPY_NULL_EBASE 0x10000

/* error number to string  */
const char* snpy_snull_strerror(int errnum) {

    return "unknown error";

}

FILE *log_file;


static int do_snap(const char *arg, int arg_size);
static int do_export(const char *arg, int arg_size);
static int do_import(const char *arg, int arg_size);
static int do_diff(const char *arg, int arg_size);
static int do_patch(const char *arg, int arg_size);


static int do_snap(const char *arg, int arg_size) {
    int rc;
    char str_buf[64];
    int status = 0;
    const char* status_msg = NULL;

    time_t snap_start = time(NULL);

    /*
     * do actually snapshot
     */

    time_t snap_fin = time(NULL);

    sprintf(str_buf, "%d", status);
    rc = kv_put_sval("meta/status", str_buf, sizeof str_buf, NULL); 
    status_msg = snpy_snull_strerror(status);
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
    time_t start, fin;
    int status = 0;
    int status_msg[1024];
    char str_buf[64];
    

    /* prepare rbd image handle */
    start = time(NULL);
    
    fin = time(NULL);
    if ((rc = update_export_arg(arg, start, fin))) {
        fprintf(log_file, "update_export_arg: %d.", rc);
    }

    kv_put_ival("meta/status", status, NULL);
    strerror_r(status, str_buf, sizeof str_buf);
    kv_put_sval("meta/status_msg", str_buf, sizeof str_buf, NULL);

    return -status;

}


static int do_import(const char *arg, int arg_size) {
    int rc;
    time_t start, fin;
    int status = 0;
    char status_msg[1024] = "";
    
    kv_put_ival("meta/status", status, NULL);
    kv_put_sval("meta/status_msg", status_msg, sizeof status_msg, NULL);

    fprintf(log_file, status_msg);
    return -status;

}

static int do_diff(const char *arg, int arg_size) {
    return 0;
}

static int do_patch(const char *arg, int arg_size) {
    return 0;
}


int main(void) {
    int rc;
    char cmd[32];
    char arg[4096];
    char id_buf[64];
    int job_id = -1;
    /* open log */
    log_file = fopen("meta/log", "w");
    if (!log_file) {
        rc = -errno;
        fprintf(stderr, "can not open log to write.\n"); 
        goto err_out;
    }
    defer { fclose(log_file); }

    if ((rc = kv_get_sval("meta/cmd", cmd, sizeof cmd, NULL))) 
        goto err_out;
    if ((rc = kv_get_sval("meta/id", id_buf, sizeof id_buf, NULL)))
        goto err_out;
    job_id = atoi(id_buf);

    if (kv_get_sval("meta/arg", arg, sizeof arg, NULL))
        goto err_out;

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
    if (!rc) 
        return job_id;
    else 
        return rc;
}
