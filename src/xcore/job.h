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

#ifndef SNPY_JOB_H
#define SNPY_JOB_H
#include <mysql.h>

snpy_job_t *snpy_job_alloc(int size);

void snpy_job_free(snpy_job_t *j);

int snpy_job_get(MYSQL *db_conn, snpy_job_t **job_ptr, int job_id);
int snpy_job_get_partial(MYSQL *db_conn, snpy_job_t *job, int job_id) ;
int snpy_job_update_state(MYSQL *db_conn, const snpy_job_t *job, 
                          int who, const char *proc,
                          int in_state, int out_state,
                          int status, 
                          const char *msg_val_fmt, ...) ;

#endif
