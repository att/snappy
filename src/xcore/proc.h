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

#ifndef SNPY_PROCESSOR_H
#define SNPY_PROCESSOR_H
#include "db.h"



typedef int (*job_proc_t) (MYSQL*, int) ;

typedef struct job_proc_entry {
    const char *name;
    job_proc_t proc;
} proc_tab_entry_t;

int bk_single_sched_proc (MYSQL *, int);
int bk_single_full_proc (MYSQL *, int);
int bk_single_incr_proc (MYSQL *, int);
int rstr_single_full_proc (MYSQL *, int);
int snap_proc (MYSQL *, int);
int export_proc (MYSQL *, int);
int diff_proc (MYSQL *, int);
int patch_proc (MYSQL *, int);
int put_proc (MYSQL *, int);
int get_proc (MYSQL *, int);

job_proc_t proc_get_job_proc(const char *job);


int proc_get_name_from_ec (const char *ec, int ec_len,
                           char *pn, int pn_len) ;
#endif
