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

#ifndef SNPY_LOG_H
#define SNPY_LOG_H

#include <time.h>
#include <stdarg.h>
#define SNPY_LOG_MSG_SIZE 2048

typedef struct log_rec {
    int who;         /*which job changed it? 0 for human */
    char proc[32];
    int state[2];   /* state[0]: state in, state[1]: state out */
    time_t ts;
    int status;
    char msg[SNPY_LOG_MSG_SIZE];
} log_rec_t;

/*
int log_make_rec_buf(char *buf, int buf_size,
                     log_rec_t *rec);

int log_add_rec_buf(char *log_buf, int log_buf_size,
                     const char *rec_buf, int rec_buf_size);

int log_get_rec_buf(const char *slog_buf, int log_buf_size, int idx,
                     char *log_rec_buf, int log_rec_buf_size

int log_parse_rec_buf(const char *log_rec_buf, int log_rec_buf_len,
                      log_rec_t *rec);
*/
int log_add_rec(char *log_buf, int log_buf_size, log_rec_t *rec);
int log_msg_add_errmsg(char *msg_buf, int msg_buf_size, int status);
 
int log_add_rec_va(char *log_buf, int log_buf_size, log_rec_t *rec, 
                   const char *msg_val_fmt, va_list ap);

int log_get_val_by_path(const char *log_buf, int log_buf_size, 
                        const char *path, 
                        void* val, int val_size) ;
#endif
