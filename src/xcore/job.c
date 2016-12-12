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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mysql.h>
#include <errno.h>
#include <stdarg.h>

#include "error.h"
#include "snappy.h"
#include "job.h"
#include "db.h"
#include "util.h"
#include "log.h"

snpy_job_t *snpy_job_alloc(int size) {
    snpy_job_t *r = NULL;
    r =  calloc(1, sizeof *r + size);
    if (r) r->buf_size = size;
    return r;
}

void snpy_job_free(snpy_job_t *j) {
    free(j);
}

enum snappy_job_col_num {
    DB_COL_ID = 0,
    DB_COL_SUB,
    DB_COL_NEXT,
    DB_COL_PARENT,
    DB_COL_GRP,
    DB_COL_ROOT,
    DB_COL_STATE,
    DB_COL_DONE,
    DB_COL_RESULT,
    DB_COL_POLICY,
    DB_COL_FEID,
    DB_COL_LOG,
    DB_COL_ARG0,
    DB_COL_ARG1,
    DB_COL_ARG2,
    DB_COL_ARG3,
    DB_COL_ARG4,
    DB_COL_ARG5,
    DB_COL_ARG6,
    DB_COL_ARG7,
    DB_COL_END
};

int snpy_job_get_partial(MYSQL *db_conn, snpy_job_t *job, int job_id) {
    if(!db_conn || !job) 
        return -EINVAL;

    MYSQL_RES *result = NULL;
    int rc; 
    int status = 0;

    const char *sql_fmt_str = 
        "select id, sub, next, parent, grp, root, "
        "state, result, policy from snappy.jobs where id = %d;";
    unsigned long long row_cnt;
    if (db_exec_sql(db_conn, 1, &row_cnt, 1, sql_fmt_str, job_id)) {
        status = SNPY_EDBCONN;
        goto free_result;
    }

    if ((result = mysql_store_result(db_conn)) == NULL ) {
        status = SNPY_EDBCONN;
        return status;
    }
 
    MYSQL_ROW row;
    unsigned long *col_lens;
    /* check if result is valid */
    if ( mysql_num_rows(result) != 1  ||
         mysql_num_fields(result) != 9 ||
         !(row = mysql_fetch_row(result)) || 
         !(col_lens = mysql_fetch_lengths(result))) {
        status = SNPY_EINVREC;
        goto free_result;
    }
  
#define SET_INT_VAL(field, col) do {           \
    if (col_lens[DB_COL_##col] == 0) {          \
        status = 2;                                     \
        goto free_result;                             \
    }                                                   \
    else                                                \
        job->field = atoi(row[DB_COL_##col]);   \
} while (0)
    SET_INT_VAL(id, ID);
    SET_INT_VAL(sub, SUB);
    SET_INT_VAL(next, NEXT);
    SET_INT_VAL(parent, PARENT);
    SET_INT_VAL(grp, GRP);
    SET_INT_VAL(root, ROOT);
    SET_INT_VAL(state, STATE);
    SET_INT_VAL(result, RESULT);
    SET_INT_VAL(policy, POLICY);

#undef SET_INT_VAL
free_result:
    mysql_free_result(result);
    return status;
   
}

/* create a job object from db record
 * return code:
 *  0: success
 *  1: no mem
 *  2: db error
 *  3: param error
 */

int snpy_job_get(MYSQL *db_conn, snpy_job_t **job_ptr, int job_id) {
    int rc; 
    int status = 0;

    if (!job_ptr) return 3;
    *job_ptr = NULL;
    MYSQL_RES *result = NULL;

    const char *sql_fmt_str = 
        "select id, sub, next, parent, grp, root, "
        "state, done, result, policy, "         
        "feid, log, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7 "
        "from snappy.jobs where id = %d;";
    unsigned long long row_cnt;
    if (db_exec_sql(db_conn, 1, &row_cnt, 1, sql_fmt_str, job_id)) {
        status = 2;
        goto free_result;
    }

    if ((result = mysql_store_result(db_conn)) == NULL ) {
        status = 2;
        return status;
    }
 
    MYSQL_ROW row;
    unsigned long *col_lens;
    /* check if result is valid */
    if ( mysql_num_rows(result) != 1  ||
         mysql_num_fields(result) != DB_COL_END ||
         !(row = mysql_fetch_row(result)) || 
         !(col_lens = mysql_fetch_lengths(result))) {
        status =2;
        goto free_result;
    }
    
    int policy = atoi(row[DB_COL_POLICY]); /* TODO: use stroll */

    int i = 0, job_buf_size = 0;
    job_buf_size += col_lens[DB_COL_FEID] + 1;
    job_buf_size += col_lens[DB_COL_LOG] + 1;
    for (i = 0; i < DB_COL_END - DB_COL_ARG0; i++) {
        if ( policy & (1 << i)) {
            job_buf_size += col_lens[DB_COL_ARG0+i] + 1;
        }
    }
    
    snpy_job_t *job = snpy_job_alloc(job_buf_size);
    if (!job) {
        status = 1; goto free_result;
    }

#if 0   
#define SET_INT_VAL(field, col) do { \
    if (col_lens[DB_COL_##col] == 0) \
        job->field = 0; \
    else \
        job->field = atoi(row[DB_COL_##col]); \
} while (0)
#endif 

#define SET_INT_VAL(field, col) do {           \
    if (col_lens[DB_COL_##col] == 0) {          \
        status = 2;                                     \
        goto free_job;                             \
    }                                                   \
    else                                                \
        job->field = atoi(row[DB_COL_##col]);   \
} while (0)

#define SET_STR_VAL(field, col) do { \
    if (job->alloc_size+col_lens[DB_COL_##col]+1 > job->buf_size) { \
        status = 1; \
        goto free_job; \
    }  \
    else {                                                  \
        char *p = job->buf + job->alloc_size;   \
        int len = col_lens[DB_COL_##col]; \
        memcpy(p, row[DB_COL_##col], len); \
        p[len] = 0; \
        job->alloc_size += len+1;              \
        job->field = p;                                     \
        job->field##_size = len+1; \
    } \
} while(0)

#define SET_ARG(n) do { \
    if (!(policy & (1 << n))) { \
        continue;  \
    } \
    if (job->alloc_size+col_lens[DB_COL_ARG##n]+1 > job->buf_size) { \
        status = 1; \
        goto free_job; \
    } \
    else {                                                  \
        char *p = job->buf + job->alloc_size;   \
        int len = col_lens[DB_COL_ARG##n]; \
        memcpy(p, row[DB_COL_ARG##n], len);                     \
        p[len] = 0; \
        job->alloc_size += len+1; \
        job->argv[n] = p;  \
        job->argv_size[n] = len+1; \
    } \
} while(0)


    SET_INT_VAL(id, ID);
    SET_INT_VAL(sub, SUB);
    SET_INT_VAL(next, NEXT);
    SET_INT_VAL(parent, PARENT);
    SET_INT_VAL(grp, GRP);
    SET_INT_VAL(root, ROOT);
    SET_INT_VAL(state, STATE);
    SET_INT_VAL(result, RESULT);
    SET_INT_VAL(policy, POLICY);
    SET_STR_VAL(feid, FEID);
    SET_STR_VAL(log, LOG);
    SET_ARG(0);
    SET_ARG(1);
    SET_ARG(2);
    SET_ARG(3);
    SET_ARG(4);
    SET_ARG(5);
    SET_ARG(6);
    SET_ARG(7);

#undef SET_INT_VAL
#undef SET_STR_VAL

    *job_ptr = job;

free_job:
    if (status) {
        snpy_job_free(job);
        *job_ptr = NULL;
    }
free_result:
    mysql_free_result(result);
    return status;
}

/*
 *  snpy_job_update_state() - update job state and the log
 *
 *  @db_conn: datatbase connection
 *  @job: target job to update
 *  @who: the job that changed the state
 *  @proc: the proc that changed the state
 *  @in_state: starting state 
 *  @out_state: end state
 *  @rc: the status code of current execution of processor
 *  @msg: buffer containing the extended message
 *  @msg_size: sizeof the message buf
 *
 */

int snpy_job_update_state(MYSQL *db_conn, const snpy_job_t *job,
                          int who, const char *proc,
                          int in_state, int out_state,
                          int status,
                          const char *msg_val_fmt, ...) {
    int rc;
    if (!job)
        return -EINVAL;


    /* prepare for log */
    struct log_rec rec = {
        .who = who,
        .proc = "",
        .state = {in_state, out_state},
        .ts = time(NULL),
        .status = status,
        .msg = ""
    };
    

    char log_buf[SNPY_LOG_SIZE]="";

    if (strlcpy(rec.proc, proc, sizeof rec.proc) >= sizeof log_buf)
        return -EMSGSIZE;
    if (job->log && strlcpy(log_buf, job->log, sizeof log_buf) >= sizeof log_buf)
        return -EMSGSIZE;

    va_list ap;
    va_start(ap, msg_val_fmt);
    if ((rc = log_add_rec_va(log_buf, sizeof log_buf, &rec, msg_val_fmt, ap))) {
        return rc;
    }
    va_end(ap);

    if ((rc = db_update_int_val(db_conn, "state", job->id, out_state))) {
        return rc;
    }

    if ((out_state & BIT(SNPY_STATE_BIT_DONE)) && 
        (rc = db_update_int_val(db_conn, "done", job->id, 1))) {
        return rc;
    }

    if ((rc = db_update_str_val(db_conn, "log", job->id,  log_buf))) {
        return rc;
    }

    if ((rc = db_update_int_val(db_conn, "result", job->id,  status))) {
        return rc;
    }

    return 0;
}
