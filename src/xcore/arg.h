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

#ifndef SNAPPY_ARG_H
#define SNAPPY_ARG_H
int ec_get_proc_name (const char *ec, int ec_len, char *pn, int pn_size);


int ec_set_proc (const char *ec, int ec_len, const char *pn, int pn_len, char
                    *new_ec, int new_ec_size) ;

int arg_get_value(const char *arg, int arg_size, 
                  const char *key, int key_len, 
                  char *val, int val_size) ;

#endif
