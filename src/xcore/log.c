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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "snappy.h"
#include "json.h"
#include "snpy_util.h"
#include "error.h"


#include "log.h"

#if 0
/* log_make_rec_buf() - make a state log record string from the struct
 *
 * @buf_size: the size of the buffer
 *
 * return: 0 - success
 */
int log_make_rec_buf(char *buf, int buf_size, 
                     log_rec_t *r) {
    const char * rec_fmt = "[%d, \"%s\", %d, %d, %lld, %d, %s]";


    int rc = snprintf(buf, buf_size, rec_fmt,
                      r->id, r->proc, r->state[0], r->state[1], r->ts,
                      r->rc, r->ext_msg);
    if (rc >= buf_size) return -ERANGE;
    return 0;

} 
/* state_add_log_entry() - add the log entry to the top of the state change log 
 *
 * @log_buf: 
 * @log_buf_size: 
 * @rec_buf: 
 * @rec_buf_size: 
 *
 * Return:  0 - success, 1 - log buffer too small, 2 - too many json tokens
 * */

int log_add_rec_buf(char *log_buf, int log_buf_size,
                    const char *rec_buf, int rec_buf_size) {

    jsmn_parser parser;
    jsmn_init(&parser);
    

    int log_buf_len = strnlen(log_buf, log_buf_size);
    if (log_buf_len == log_buf_size) return -EINVAL;
    /* if log is empty, just create the first item */
    if (log_buf_len == 0 ) {
        if (snprintf(log_buf, log_buf_size, "[%s]", rec_buf) >= log_buf_size)
            return -ERANGE;
        else 
            return 0;
    }
    
    /* add record in front of the existing records */
    char tmp_buf[SNPY_LOG_SIZE];
    jsmntok_t toks[256];
    memcpy(tmp_buf, log_buf, log_buf_len);
    tmp_buf[log_buf_len] = 0;
    if (jsmn_parse(&parser, tmp_buf, log_buf_len, toks, 256) < 0) {
        return -ERANGE;
    }
    
    int rc = snprintf(log_buf, log_buf_size, "[%s,%s",
             rec_buf, &tmp_buf[toks[0].start+1]);
    
    if (rc >= log_buf_size) return -ERANGE;
    return 0;
}



int log_get_rec_buf(const char *log_buf, int log_buf_size, int idx,
                     char *rec_buf, int rec_buf_size) {

    jsmntok_t toks[256];
    jsmn_parser parser;
    jsmn_init(&parser);
    int log_buf_len = strnlen(log_buf, log_buf_size);
    if (log_buf_len == log_buf_size) return -EINVAL;
    if (jsmn_parse(&parser, log_buf, log_buf_len, toks, 256) < 0) {
        return 2;
    }
    int rec_num = toks[0].size;
    if (idx >= rec_num)  return 1;
    int i, j;
    for (i = 0, j = -1; i < rec_num; i++) {
        if (toks[i].parent == 0) j++;
        if (j == idx) break;
    }
    
    int rec_buf_len = toks[i].end - toks[i].start;
    if (rec_buf_len  < rec_buf_size) {
        *((char *)(mempcpy(rec_buf, log_buf+toks[i].start, rec_buf_len))) = 0;
        return 0;
    } else return -ERANGE;


}

int log_parse_rec_buf(const char *rec_buf, int rec_buf_len,
                       log_rec_t *rec) {
    jsmn_parser parser;
    jsmn_init(&parser);

    jsmntok_t toks[8];
    
    if (!rec || jsmn_parse(&parser, rec_buf, rec_buf_len, toks, 8) < 0 || 
       toks[0].size != 7) {
        return -EINVAL;
    }
   
    rec->id = strtol(&rec_buf[toks[1].start], NULL, 0);
    rec->old_state = strtol(&rec_buf[toks[2].start], NULL, 0);
    rec->new_state = strtol(&rec_buf[toks[3].start], NULL, 0);
    rec->timestamp = strtoll(&rec_buf[toks[4].start], NULL, 0);
    rec->res_code = strtoll(&rec_buf[toks[5].start], NULL, 0);
    int len = toks[6].end - toks[6].start;
    *((char *)mempcpy(rec->res_msg, &rec_buf[toks[6].start], len)) = 0;
    len = toks[7].end - toks[7].start;
    *((char *)mempcpy(rec->extra, &rec_buf[toks[7].start], len)) = 0;
    return 0;
}
#endif



int log_add_rec(char *log_buf, int log_buf_size, log_rec_t *rec) {
    int rc = 0;
    char rec_buf[SNPY_LOG_SIZE];
    int log_buf_len = strnlen(log_buf, log_buf_size);
    rc = snprintf(log_buf+log_buf_len, log_buf_size, 
                  "%d|%s|%d|%d|%ld|%d|%s\n",
                  rec->who, rec->proc, rec->state[0], rec->state[1],
                  rec->ts, rec->status, rec->msg);
    if (rc == log_buf_size - log_buf_len) 
        return -ERANGE;
    return 0;
}

int log_msg_add_errmsg(char *msg_buf, int msg_buf_size, int status) {
    int msg_buf_len;
    if (!msg_buf) 
        return -EINVAL;
    if ((msg_buf_len = strnlen(msg_buf, msg_buf_size)) == msg_buf_size) 
        return -EINVAL;
    int avail_buf_size = msg_buf_size - msg_buf_len;
    if (snprintf(msg_buf+msg_buf_len, avail_buf_size, 
                 "\"errmsg\":\"%s\"", snpy_strerror(status)) 
        >= avail_buf_size) {
        msg_buf[msg_buf_len] = 0;
        return -EMSGSIZE;
    }

    return 0;
}


int log_add_rec_va(char *log_buf, int log_buf_size, log_rec_t *rec, 
                     const char *msg_val_fmt, va_list ap) {
    int i;
    int error; 
    struct jsonxs xs;
    struct json *js = json_open(JSON_F_NONE, &error);                         
    if (!js) 
        return -error; 
    if (!log_buf[0]) {
        log_buf[0] = '[';
        log_buf[1] = ']';
    }
    int rc = json_loadstring(js, log_buf);                                                       
    if (rc) {
        error = rc;
        goto close_js; 
    }
    if ((error = json_enter(js, &xs))) 
        goto js_error;

    int rec_cnt = json_count(js, "."); 
    json_setnumber(js, rec->who, "[#][0]", rec_cnt);
    json_setstring(js, rec->proc, "[#][1]", rec_cnt);
    json_setnumber(js, rec->state[0], "[#][2]", rec_cnt);
    json_setnumber(js, rec->state[1], "[#][3]", rec_cnt);
    json_setnumber(js, rec->ts, "[#][4]", rec_cnt);
    json_setnumber(js, rec->status, "[#][5]", rec_cnt);
    json_setobject(js, "[#][6]", rec_cnt);
    
    char err_buf[256]="";
    if (rec->status) {
        json_setstring(js, snpy_strerror(rec->status), 
                       "[#][6].$", rec_cnt, "err_msg");
    }
    if (msg_val_fmt == NULL) 
        goto close_js;
    
    /* handling key value in the msg part */
    const char *key;
    const char *sval;
    int ival;
    double fval;

    for (i = 0; i < 32 && msg_val_fmt[i]; i ++ ) {
        key =  va_arg(ap, const char*);
        switch (msg_val_fmt[i]) {
        case 's':
            sval = va_arg(ap, const char*);
            if (sval && sval[0]) 
                json_setstring(js, sval, "[#][6].$", rec_cnt, key);
            break;
        case 'i':
            ival = va_arg(ap, long);
            json_setnumber(js, ival, "[#][6].$", rec_cnt, key);
            break;
        case 'f':
            fval = va_arg(ap, double);
            json_setnumber(js, fval, "[#][6].$", rec_cnt, key);
            break;
        default:
            rc = -EINVAL;
            break;

        }

    }
    json_leave(js, &xs);
    goto close_js;
js_error:
    json_leave(js, &xs);
    printf("%d: %s\n", error, json_strerror(error));
close_js: 
    json_printstring(js, log_buf, log_buf_size, 0, &error);
    if (rc) 
        printf("%d: %s\n", error, json_strerror(error));
    json_close(js);                                                                           
    return rc;                                                                                
} 



int log_get_val_by_path(const char *log_buf, int log_buf_size, 
                        const char *path, 
                        void* val, int val_size) {

    if (!log_buf || !path || !val)
        return -EINVAL;

    int i;
    int status = 0; 
    struct jsonxs xs;
    struct json *js = json_open(JSON_F_NONE, &status);                         
    
    if (!js) 
        return -status; 

    int rc = json_loadstring(js, log_buf);                                                      
    if (rc) {
        status = rc;
        goto close_js; 
    }
    if (!json_exists(js, path)) {
        status = SNPY_ELOG;
        goto close_js;
    }

    if (json_type(js, path) == JSON_T_STRING) {
        strlcpy(val, json_string(js, path), val_size);
    } else if (json_type(js,path) == JSON_T_NUMBER) {
        if (val_size == 8) 
            *(double *)val = json_number(js, path);
        else  
            status = EINVAL;
    } else {
        status = SNPY_ELOG;
    }

 close_js: 
    if (status) 
        printf("%d: %s\n", status, json_strerror(status));
    json_close(js);                                                                           
    return -status;   
}


#if TEST_MAIN

int main(void) {
    
    log_rec_t rec = {
        .id = 1,
        .proc = "snap",
        .ts = 123232323,
        .status = 0,
        .msg = "{\"rc_msg\": \"failed\"}"
    };

    char buf[SNPY_LOG_SIZE] = "[[0, \"bk_single_full\", 1, 2, 1123232323, 0, {\"rc_msg\": \"success\"}]]";


    char log[4096];
    char log_ent_buf[4096];
    log_add_rec(buf, sizeof buf, &rec);
    return 0;

}

#endif
