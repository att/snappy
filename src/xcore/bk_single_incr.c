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


#include "snappy.h"
#include "db.h"
#include "arg.h"
#include "log.h"
#include "job.h"


#include "bk_single_incr.h"

static void proc_created(MYSQL *db_conn, snpy_job_t *job);
static void proc_done(MYSQL *db_conn, snpy_job_t *job);
static void proc_ready(MYSQL *db_conn, snpy_job_t *job);
static void proc_blocked(MYSQL *db_conn, snpy_job_t *job);
static void proc_zombie(MYSQL *db_conn, snpy_job_t *job);


static void proc_created(MYSQL *db_conn, snpy_job_t *job) {
    
    return;
}
static void proc_done(MYSQL *db_conn, snpy_job_t *job) {

    return;
}
static void proc_ready(MYSQL *db_conn, snpy_job_t *job) {

    return;
}
static void proc_blocked(MYSQL *db_conn, snpy_job_t *job) {

    return;
}
static void proc_zombie(MYSQL *db_conn, snpy_job_t *job) {

    return;
}




/*
 * return:
 *  0 - success
 *  1 - database error
 *  2 - job record missing/invalid.
 */

int bk_single_incr_proc (MYSQL *db_conn, int job_id) {  
    int rc; 
    int status = 0;
    snpy_job_t *job;
    rc = mysql_query(db_conn, "start transaction;");
    if (rc != 0)  return 1;
    
    db_lock_job_tree(db_conn, job_id);
    rc = snpy_job_get(db_conn, &job, job_id);
    if (rc) return 1;
    int sched_state = SNPY_GET_SCHED_STATE(job->state);
    switch (sched_state) {
    case SNPY_SCHED_STATE_CREATED:
        proc_created(db_conn, job);
        break;

    case SNPY_SCHED_STATE_DONE:
        proc_done(db_conn, job);
        break;  /* shouldn't be here*/

    case SNPY_SCHED_STATE_READY:
        proc_ready(db_conn, job);
        break; 

    case SNPY_SCHED_STATE_BLOCKED:
       proc_blocked(db_conn, job);
        break;

    case SNPY_SCHED_STATE_ZOMBIE:
        proc_zombie(db_conn, job);
        break;
    }
    

    snpy_job_free(job); job = NULL;

    if (status == 0) 
        rc = mysql_commit(db_conn);
    else 
        rc = mysql_rollback(db_conn);

    return rc?1:status;
}




