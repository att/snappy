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

#include <string.h>
#include <stdlib.h>
#include <errno.h>

//#include "jsmn.h"

#include "arg.h"
#include "util.h"


#define MAX_TOKENS 1024
#if 0
/*
 * return value: 
 * 0: success
 * 1: json string parse error.
 * 2: pn_size too small.
 */
int ec_get_proc_name (const char *ec, int ec_len, 
                      char *pn, int pn_size) {
    jsmn_parser parser;

    jsmntok_t toks[MAX_TOKENS];
    jsmn_init(&parser);
    int rc = jsmn_parse(&parser, ec, ec_len, toks, ARRAY_SIZE(toks));
    if (rc < 0 ) {
        return 1;
    }
    if (toks[0].type != JSMN_ARRAY) {
        return 1;
    }
    if (toks[0].size != 1) { /*only 1 config allowed now */
        return 1;
    }
    if (toks[1].type != JSMN_OBJECT) {
        return 1;
    }
    int i = 0;
    for (i = 1; i < rc; i++) {
        if (!jsmn_strcmp("proc", strlen("proc"), ec, &toks[i]) && 
            toks[i].size == 1) {
            int j = i+1;
            int tok_len = toks[j].end - toks[j].start;
            if (pn_size <= tok_len) 
                return 2;
            else {
                strncpy(pn, ec+toks[j].start, pn_size);
                pn[tok_len] = 0;
                return 0;
            }
        }
    }

    return 1;

}

/*
 * ec_strrep - given a string replace the substring starts from @start and
 * ends at @end-1 with @rep
 *
 * returns:
 *  0: success
 *  -1: params error
 *
 */

int ec_strrep(const char *in, int in_size, char *out, int out_size,
              int start, int end, const char *rep, int rep_size) {


    int in_len = strnlen(in, in_size);
    int rep_len = strnlen(rep, rep_size);

    /* check the parameters */
    if ( in_len == in_size || start < 0 || end > in_len || end <= start
         || in_len+rep_len-(end-start) < out_size)
        return -1;

    memcpy(out, in, start);
    memcpy(out + start, rep, rep_len);
    memcpy(out + start + rep_len, in+end, in_len-end);
    out[start+rep_len+in_len-end] = 0;
    return 0;
}

int ec_set_proc(const char *ec, int ec_size, const char *pn, int pn_len,
                char *new_ec, int new_ec_size) {
    jsmn_parser parser;
    
    int ec_len = strnlen(ec, ec_size);
    if (ec_len >= ec_size) return 1;
    jsmntok_t toks[MAX_TOKENS];
    jsmn_init(&parser);
    int rc = jsmn_parse(&parser, ec, ec_len, toks, ARRAY_SIZE(toks));
    if (rc < 0 ) {
        return 1;
    }
    if (toks[0].type != JSMN_ARRAY) {
        return 1;
    }
    if (toks[0].size != 1) { /*only 1 config allowed now */
        return 1;
    }
    if (toks[1].type != JSMN_OBJECT) {
        return 1;
    }

    int i = 0;
    for (i = 1; i < rc; i++) {
        if (!jsmn_strcmp("proc", 4, ec, &toks[i]) && 
            toks[i].size == 1 && toks[i].parent == 1) {
            int j = i+1;
            int tok_len = toks[j].end - toks[j].start;
            memset(new_ec, 0, new_ec_size);
            memcpy(new_ec, ec, toks[j].start);
            memcpy(new_ec+toks[j].start, pn, pn_len);
            memcpy(new_ec+toks[j].start+pn_len, &ec[toks[j].end], ec_len-ec[toks[j].end] );
            return 0;
        }
    }
    
    return 1;

}



int ec_get_active_ec(const char *ec_buf, int ec_buf_size, int policy, 
                     const char *active_ec, int active_ec_len) {
    
    if (!ec_buf) goto err_out;
    jsmntok_t toks[MAX_TOKENS];
    jsmn_parser parser;
    jsmn_init(&parser);
    int rc = jsmn_parse(&parser, ec_buf, strnlen(ec_buf, ec_buf_size), 
                        toks, ARRAY_SIZE(toks));
    
    

err_out:
    return 1;
}
/* arg_get_value() - use to get the the value of a key in a top level json 
 * object
 *
 */

int arg_get_value(const char *arg, int arg_size, 
                  const char *key, int key_len, 
                  char *val, int val_size) {

    int rc;
    int status;
    jsmntok_t *toks = NULL;
    jsmn_parser parser;

    jsmn_init(&parser);
    

    int ntok = jsmn_parse(&parser, arg, arg_size, NULL, 0);
    if (ntok < 0) { 
        status = EINVAL; 
        goto err_out;
    }

    toks = malloc(ntok * sizeof *toks);
    if (!toks) { 
        status = ENOMEM;
        goto err_out;
    }
    jsmn_init(&parser);
    rc = jsmn_parse(&parser, arg, arg_size, toks, ntok);
    if (rc < 0 || toks[0].type != JSMN_OBJECT) {
        status = EINVAL;
        goto free_toks;
    }
    int i;
    for (i = 0; i < rc; i ++) {
        if (toks[i].size == 1 && toks[i].parent == 0 && 
            !jsmn_strcmp(key, key_len, arg, &toks[i])) {

            int len = toks[i+1].end - toks[i+1].start;
            if (len >= val_size) {
                status = ERANGE;
                goto free_toks;
            }
            memcpy(val, &arg[toks[i+1].start], len); val[len] = 0;
            return 0;
        }
    }

    status = ESRCH;
free_toks:
    free(toks);
err_out:
    return -status;
}


#endif 
