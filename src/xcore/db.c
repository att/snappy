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
#include <stdarg.h>
#include <errno.h>


#include "snpy_util.h"
#include "db.h"
#include "snappy.h"
#include "log.h"
#include "job.h"
#include "ciniparser.h"

static int db_initialized = 0;
static MYSQL db_conn;

#define SQL_BUFSIZE  64*1024
#define KEEP_RES    0x1

static snappy_db_conf_t snappy_db_conf = {
    .host = "localhost",
    .user = "root",
    .pass = "snappy",
    .port = 3306,
    .db_name = "snappy"
};

MYSQL *create_mysql_conn (snappy_db_conf_t * info, MYSQL *conn) {
    if (!info || !conn) 
        return NULL;

    mysql_init(conn);
    mysql_options(conn, MYSQL_INIT_COMMAND,"SET innodb_lock_wait_timeout=1");

    conn = mysql_real_connect(conn, 
                            info->host,
                            info->user,
                            info->pass,
                            info->db_name,
                            info->port,
                            0,
                            CLIENT_MULTI_STATEMENTS);

    return conn;
}


/* db_conn_init() - initialize databse connect;
 *
 */
int db_conn_init(void) {
    
    strlcpy(snappy_db_conf.host, 
            ciniparser_getstring(snpy_conf, "database:server", "localhost"),
            sizeof snappy_db_conf.host);
    strlcpy(snappy_db_conf.user, 
            ciniparser_getstring(snpy_conf, "database:user", "snappy"),
            sizeof snappy_db_conf.user);
    strlcpy(snappy_db_conf.pass, 
            ciniparser_getstring(snpy_conf, "database:pass", "snappy"),
            sizeof snappy_db_conf.pass);
    snappy_db_conf.port =  ciniparser_getint(snpy_conf, "database:port", 3306);

    if (!db_initialized && create_mysql_conn(&snappy_db_conf, &db_conn)) {
        db_initialized = 1;
        return 0;
    }
    return 1;
}

MYSQL* db_get_conn(void) {
    return &db_conn;
}

/*
 * sql_simple_exec() - execute a sql query string in printf style
 *
 * @db_conn: valid database connection
 * @flags: 
 *  KEEP_RES - should results be consumed (so can do next query) 
 * @sql_fmt_str: query format string.
 *
 * return:
 *  0 - success
 *  1 - param error
 *  2 - query buf too small
 *  3 - db error.
 */

int db_exec_sql(MYSQL *db_conn, int flags,
                unsigned long long *row_cnt, int row_cnt_size,
                const char *sql_fmt_str, ...) {
    char sql_buf[SQL_BUFSIZE];
    va_list ap;
    int rc;
    
    if (!db_conn) 
        return -EINVAL;

    va_start(ap, sql_fmt_str);
    rc = vsnprintf(sql_buf, sizeof sql_buf, sql_fmt_str, ap);
    va_end(ap);
    if (rc >= sizeof sql_buf) return -ERANGE;

    if (mysql_query(db_conn, sql_buf)) 
        return -mysql_errno(db_conn);

    if (!(flags&KEEP_RES)) {    /* need to consume result */
        int i = 0;
        do {
            /* did current statement return data? */
            MYSQL_RES *res = mysql_store_result(db_conn);
            if (res == NULL && mysql_field_count(db_conn) != 0) { 
                /* error occurred */
                return -mysql_errno(db_conn);
            } 
            /* if row_cnt are needed, store it */   
            if (row_cnt != NULL && i < row_cnt_size) {
                row_cnt[i] = mysql_affected_rows(db_conn);
            }
            mysql_free_result(res);

            /* more results? -1 = no, >0 = error, 0 = yes (keep looping) */
            if ((rc = mysql_next_result(db_conn)) > 0)
                return -mysql_errno(db_conn);
        } while (rc == 0);

    }
    return 0;
}


int db_job_update_state(MYSQL *db_conn, int job_id, int new_state) {
    return 
        db_exec_sql(db_conn, 0, NULL, 0,
                    "update snappy.jobs set state=%d where id=%d;",
                    new_state, job_id);
}

/*
 * fill a snappy job structure, but only fill structural fields:
 * id, sub, next, parent, grp, root, state
 */

int db_get_job_partial(MYSQL *db_conn, snpy_job_t *job, int job_id) {
    return 0;       
}


int db_lock_job(MYSQL *db_conn, int job_id) {

    return db_exec_sql(db_conn, 0, NULL, 0, 
                       "select id from snappy.jobs where id=%d for update",
                       job_id);
}



int db_lock_job_tree(MYSQL *db_conn, int job_id) {
    
    return db_exec_sql(db_conn, 0, NULL, 0, 
                       "select COUNT(x.id) from snappy.jobs "
                       "as x, snappy.job as y "
                       "where x.root=y.id and y.id=%d for update",
                       job_id); 
}


int db_check_sub_job_done(MYSQL *db_conn, int job_id) {
    if (!db_conn || !job_id) 
        return -1;
    const char * sql_fmt_str = 
        "select COUNT(id) from snappy.jobs where parent=%d and id!=parent and done!=0";
    char sql_query_buf[256];
    sprintf(sql_query_buf, sql_fmt_str, job_id);
    int rc;
    MYSQL_RES *res = NULL;
    MYSQL_ROW row;

    rc = mysql_query(db_conn, sql_query_buf);
    if (rc) 
        return 0;
    res = mysql_store_result(db_conn);
    if (res == NULL) {
        return 0;
    }
    row = mysql_fetch_row(res);
    if (row == NULL) {
        mysql_free_result(res);
        return 0;
    }
     
    int cnt = atoi(row[0]);
    mysql_free_result(res);    
    return cnt;
    

}

/* 
 *
 * return: -1 for error. 
 */

int db_insert_new_job (MYSQL *db_conn) {
    if (!db_conn) 
        return -1;
    
    int rc = mysql_query(db_conn, "insert into snappy.jobs () values ();");
    if (rc) return -mysql_errno(db_conn);

    int new_job_id = mysql_insert_id(db_conn);
    const char *sql_fmt_str =
        "update snappy.jobs set parent=%d, grp=%d, root=%d where id=%d;";
    char sql_query_buf[256];
    sprintf(sql_query_buf, sql_fmt_str, new_job_id, new_job_id, new_job_id, new_job_id);
    rc = mysql_query(db_conn, sql_query_buf);
    if (rc) 
        return -1;

    return new_job_id;

}

int db_get_last_sub_job(MYSQL *db_conn, int job_id) {
    if (!db_conn) 
        return -1;

    const char *sql_fmt_str = 
        "select id from snappy.jobs where parent=%d and next=0 and parent!=id;";
    char sql_query_buf[256];
    sprintf(sql_query_buf, sql_fmt_str, job_id);
    int rc;
    MYSQL_RES *res = NULL;
    MYSQL_ROW row;
    if ( (rc= mysql_query(db_conn, sql_query_buf)) ||
         !(res = mysql_store_result(db_conn)) ||
         !(row = mysql_fetch_row(res))) {
        mysql_free_result(res);
        return -1;
    }
    
    int last_sub_job = atoi(row[0]);
    return last_sub_job;
}


#if 0
/* db_add_sub_job() - add sub job for @job 
 *
 * return: 0: success, < 0: error
 */

int db_add_sub_job(MYSQL *db_conn, snpy_job_t *job) {
    if (!db_conn) return -1;
    if (job->sub != 0) 
        return -2;
    int sub_job_id = db_insert_new_job(db_conn);
    if (sub_job_id < 0) return -3;
    
    const char *sql_fmt_str =
        "update snappy.jobs set parent=%d, root=%d where id=%d;";
    char sql_query_buf[256];
    sprintf(sql_query_buf, sql_fmt_str, job->id, job->root, sub_job_id);
    if (mysql_query(db_conn, sql_query_buf)) return -3;
    
    sql_fmt_str = "update snappy.jobs set sub=%d where id=%d;";
    sprintf(sql_query_buf, sql_fmt_str, sub_job_id, job->id);
    if (mysql_query(db_conn, sql_query_buf)) return -3;
    return sub_job_id;
}

int db_add_next_job (MYSQL *db_conn, snpy_job_t *job) {
    if (!db_conn) return -1;
    if (job->next != 0) 
        return -2;
    int next_job_id = db_insert_new_job(db_conn);
    if (next_job_id < 0) return -3;
    
    const char *sql_fmt_str =
        "update snappy.jobs set parent=%d, grp=%d, root=%d where id=%d;";
    char sql_query_buf[256];
    sprintf(sql_query_buf, sql_fmt_str, job->parent, job->grp, job->root, next_job_id);
    if (mysql_query(db_conn, sql_query_buf)) return -3;
    
    sql_fmt_str = "update snappy.jobs set next=%d where id=%d;";
    sprintf(sql_query_buf, sql_fmt_str, next_job_id, job->id);
    if (mysql_query(db_conn, sql_query_buf)) return -3;
    return next_job_id;
}
#endif 

/* db_update_job() - update job record using information from @job.
 *
 *
 */
int db_update_job_partial(MYSQL *db_conn, snpy_job_t *job) {
    if (!job) 
        return -EINVAL;
    const char *sql_fmt_str = 
        "update snappy.jobs "
        "set id=%d, sub=%d, next=%d, parent=%d, grp=%d, root=%d, "
        "state=%d,done=%d,result=%d, policy=%d "
        "where id=%d";

    int rc = db_exec_sql(db_conn, 0, NULL, 0, sql_fmt_str, 
                         job->id, 
                         job->sub, 
                         job->next,
                         job->parent, 
                         job->grp, 
                         job->root, 
                         job->state,
                         job->state & BIT(SNPY_STATE_BIT_DONE),
                         job->result, 
                         job->policy,
                         job->id);
    return rc;
}

int db_update_str_val(MYSQL *db_conn, const char *col, int id, const char *val) {
    int rc = db_exec_sql(db_conn, 0, NULL, 0, 
                         "update snappy.jobs "
                         "set %s='%s' "
                         "where id=%d",
                         col, val, id);
    return rc;
}

int db_update_int_val(MYSQL *db_conn, const char *col, int id, int val) {
    int rc = db_exec_sql(db_conn, 0, NULL, 0, 
                         "update snappy.jobs "
                         "set %s=%d "
                         "where id=%d",
                         col, val, id);
    return rc;
}

int db_get_val(MYSQL *db_conn, const char *col, int id, char *val, int val_size) {
    if (!db_conn || !col || id <= 0 || !val) 
        return -EINVAL;

    int rc = db_exec_sql(db_conn, KEEP_RES, NULL, 0, 
                         "select %s from snappy.jobs "
                         "where id=%d",
                         col, id);
    if (rc) 
        return rc;
    MYSQL_RES *result = NULL;
    if ((result = mysql_store_result(db_conn)) == NULL ) {
        return  -mysql_errno(db_conn);
    }
 
    MYSQL_ROW row;
    unsigned long *col_lens;
    int status = 0;
    /* check if result is valid */
    if ( mysql_num_fields(result) != 1 ||
         !(row = mysql_fetch_row(result)) || 
         !(col_lens = mysql_fetch_lengths(result))) {
        status = mysql_errno(db_conn);
        goto free_result;
    }
    if (strlcpy(val, row[0], val_size) >= val_size) {
        status = -ERANGE;
        goto free_result;
    }
free_result:
    mysql_free_result(result);
    return status;
   
}

int db_get_ival(MYSQL *db_conn, const char *col, int id, int *val) {
    char buf[4096];
    int rc = db_get_val(db_conn, col, id, buf, sizeof buf);
    if (rc) 
        return rc;
    *val = atoi(buf);
    return 0;
    
}
