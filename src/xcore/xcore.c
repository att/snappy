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
#include <pthread.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#include "ciniparser.h"

#include "snappy.h"
#include "error.h"
#include "db.h"
#include "proc.h"
#include "conf.h"
#include "plugin.h"


#include "snpy_util.h"
#include "snpy_log.h"

static void sigchld_handler(int , siginfo_t *, void *);
static int init_signal_handler(void);
static int snappy_env_chk(void);
static int snappy_load_conf(char *);


static volatile sig_atomic_t evt_flag = 0;
static const char *snpy_conf_file = NULL;

struct snpy_log xcore_log;


int snpy_load_conf(void) {
    const char *conf_locs[] = {
        "./snappy.conf",
        "/etc/snappy.conf",
        "/var/lib/snappy/etc/snappy.conf"
    };
    int i;
    if (snpy_conf_file) 
        goto load_conf_file;

    /* choose conf file location based on priority */
    for (i = 0; i < ARRAY_SIZE(conf_locs); i ++) {
        if (!access(conf_locs[i], R_OK))
            snpy_conf_file = conf_locs[i];
    }
    
    if (!snpy_conf_file)
        return -SNPY_ECONF;

load_conf_file:

    snpy_conf = ciniparser_load(snpy_conf_file);
    if (!snpy_conf)
        return -SNPY_ECONF;
    return 0;
}

void snpy_free_conf(void) {
    free(snpy_conf);

}


static int snappy_env_chk(void) {
   
    return 0;
}


static int init_signal_handler(void) {

    struct sigaction act;
    memset(&act, 0, sizeof act);
    act.sa_sigaction = &sigchld_handler;
    act.sa_flags = SA_SIGINFO;
    
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGPIPE);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGHUP);
    
    act.sa_mask = mask;

    if (sigaction(SIGCLD, &act, NULL) < 0 ) {
        perror ("sigaction");
        return 1;
    }
    return 0;

}

static int xcore_init(void) {
    
    printf("Broker starting...\n");
    printf("Config signal handlers..\n");
    if (init_signal_handler()) {
        goto err_out;
    }
    printf("Loading config file..");
    if (snpy_load_conf()) {
        goto err_out;
    }
    printf("Establish database connection..");
    if(db_conn_init()) {
        goto err_out;
    }
    
    if (plugin_tbl_init()) 
        goto err_out;

    if (snappy_env_chk()) {
        goto err_out;
    }
    
    const char *log_fn = conf_get_log();
        
    if (snpy_log_open(&xcore_log, log_fn, 0)) {
        printf("can not open log file. \n");
        goto err_out;
    }

    snpy_log(&xcore_log, SNPY_LOG_INFO, "Broker started. ");
    return 0;
err_out:
    snpy_log(&xcore_log, SNPY_LOG_ERR, "error starting broker, exiting.");
    exit(1);
}

static void xcore_deinit(void) {
    snpy_log_close(&xcore_log);
    return;

}
 
static void sigchld_handler (int sig, siginfo_t *siginfo, void *context) {
    evt_flag = 1;
    return;
}



int main(int argc, char** argv) {
        
    if (argc == 2 && !strcmp(argv[1], "version")) {
        printf("xcore version: %s-%s, build time: %s\n", GIT_BRANCH, GIT_HASH, COMPILE_TIME);
        return 0;
    }
    xcore_init();
    
    daemon(1, 1);

    sigset_t block_sigchld;
    sigemptyset(&block_sigchld);
    sigaddset(&block_sigchld, SIGCLD);

    MYSQL *conn = NULL;
    MYSQL_RES *result = NULL;

    conn = db_get_conn();

    int cur_id = 0;
    
    while (1) {
        int rc;
        const char *sql_fmt_str = 
            "select MIN(id) from snappy.jobs "
            "where done = 0 and id > %d;";
        
        rc = db_exec_sql(conn, 1, NULL, 0, sql_fmt_str, cur_id);
        if (rc) {
            snpy_log(&xcore_log, SNPY_LOG_ERR, "query error: %s.", mysql_error(conn));
            continue;
        }
        result = mysql_store_result(conn);
        if (result == NULL) {
            continue;
        }
        if ( mysql_num_rows(result) != 1)  {
            snpy_log(&xcore_log, SNPY_LOG_ERR, "snappy job db table issue: non-unqiue id.");
            goto free_result;
        } else {
            int id;
            MYSQL_ROW row = mysql_fetch_row(result);
            unsigned long *col_lens = mysql_fetch_lengths(result);
            if (!row || !col_lens) {
                snpy_log(&xcore_log, SNPY_LOG_ERR, "query error: %s", mysql_error(conn));
                goto free_result;
            }
            if (!col_lens[0]) {
                /* we are at the end of the table */
                snpy_log(&xcore_log, SNPY_LOG_DEBUG, "Reached end of the jobs table.");
                cur_id = 0;
                goto free_result;
            }
            
            
            cur_id = atoi(row[0]);
            /* get job processor name */
            char proc_name[64]= "";
            rc = db_get_val(conn, "arg0", cur_id, proc_name, sizeof proc_name);
            if (rc) {
                snpy_log(&xcore_log, SNPY_LOG_ERR, "can not get processor name:  %d", rc);
                goto free_result;
            }

            job_proc_t proc = proc_get_job_proc(proc_name);
            /* TODO :  maybe apply a filter? */
            snpy_log(&xcore_log, 
                     SNPY_LOG_DEBUG, "processing job id: %d, proc_name: %s", 
                                cur_id, proc_name);
            if (proc == NULL) {
                snpy_log(&xcore_log, SNPY_LOG_DEBUG, 
                       "no processor defined for job proc_name: %s.\n",
                       proc_name);
                goto free_result;
            }
            if ((rc = proc(conn, cur_id))) {
                snpy_log(&xcore_log, SNPY_LOG_ERR, 
                         "error in job id: %d, processor %s: %d, %s.\n",
                        cur_id, proc_name, rc, snpy_strerror(-rc));
                goto free_result;
            }
        }

        /* handle spawned jobs
         * TODO: maintain spawned task in a separate queue and using a dedicate
         * thread to handle it
         */
         
        sigprocmask(SIG_BLOCK, &block_sigchld, NULL);
        if (evt_flag) {
            while(1) {
                int status;
                pid_t chld_pid = waitpid(-1, &status, WNOHANG);
                if (chld_pid > 0) {
                    snpy_log(&xcore_log, SNPY_LOG_DEBUG, 
                             "collected i/o job id: %d, pid: %d.\n", 
                             WEXITSTATUS(status), chld_pid);
                } else if (chld_pid == 0) {
                    snpy_log(&xcore_log, SNPY_LOG_DEBUG, "no zombie process.\n");
                    break;
                } else if (errno == ECHILD) {
                    break;
                }
            }
            evt_flag = 0;
        }
        sigprocmask(SIG_UNBLOCK, &block_sigchld, NULL);

       
free_result:
        mysql_free_result(result);
        if(cur_id == 0) 
            sleep(1);
    }
    
    mysql_close(conn);
    xcore_deinit();
    return 0;
}
