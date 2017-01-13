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

#ifndef SNPY_DB_H
#define SNPY_DB_H

#include <mysql.h>

#include "snappy.h"

typedef struct snappy_db_conf {
    char host[256];
    char user[256];
    char pass[256];
    int port;
    char db_name[256];
} snappy_db_conf_t;

MYSQL* create_mysql_conn (snappy_db_conf_t * info, MYSQL *db_conn) ;

int db_conn_init(void);
MYSQL*  db_get_conn(void) ;

int db_exec_sql(MYSQL *db_conn, int flags, 
                unsigned long long *row_cnt, int row_cnt_size, 
                const char *sql_fmt_str, ...);


int db_lock_job(MYSQL *db_conn, int job_id);
int db_lock_job_tree(MYSQL *db_conn, int job_id);
int db_check_sub_job_done(MYSQL *db_conn, int job_id);
int db_insert_new_job (MYSQL *db_conn) ;

int db_get_last_sub_job(MYSQL *db_conn, int job_id) ;
int db_add_sub_job(MYSQL *db_conn, snpy_job_t *job) ;
int db_add_next_job (MYSQL *db_conn, snpy_job_t *job) ;
int db_update_state_chg_log (MYSQL *db_conn, int job_id, char *log_buf, int buf_size) ;
int db_job_update_state(MYSQL *db_conn, int job_id, int new_state);
int db_update_job(MYSQL *db_conn, snpy_job_t *job);
int db_update_job_partial(MYSQL *db_conn, snpy_job_t *job);
int db_update_str_val(MYSQL *db_conn, const char *col, int id, const char *val) ;
int db_update_int_val(MYSQL *db_conn, const char *col, int id, int val);

int db_get_ival(MYSQL *db_conn, const char *col, int id, int *val);
int db_get_val(MYSQL *db_conn, const char *col, int id, char *val, int val_size);
#endif
