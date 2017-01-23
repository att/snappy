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

#ifndef SNPY_ERROR_H
#define SNPY_ERROR_H

#include "snappy.h"


static const char *snpy_errmsg_tab[] = {
    [SNPY_ENOPROC - SNPY_EBASE] = "snappy - no processor found",
    [SNPY_EDBCONN - SNPY_EBASE] = "snappy - database connection error",
    [SNPY_EBADJ - SNPY_EBASE] = "snappy - bad job status",
    [SNPY_EENVJ - SNPY_EBASE] = "snappy - job enviroment setup error",
    [SNPY_ESPAWNJ - SNPY_EBASE] = "snappy - error spawn job",
    [SNPY_ESTATJ - SNPY_EBASE] = "snappy - invalid job state",
    [SNPY_EINVREC - SNPY_EBASE] = "snappy - invalid record",
    [SNPY_ESUB - SNPY_EBASE] = "snappy - sub job error",
    [SNPY_ENEXT - SNPY_EBASE] = "snappy - next job error",
    [SNPY_EPROC - SNPY_EBASE] = "snappy - processor error",
    [SNPY_EPLUG - SNPY_EBASE] = "snappy - plugin return error",
    [SNPY_EARG - SNPY_EBASE] = "snappy - job argument error",
    [SNPY_ECONF - SNPY_EBASE] = "snappy - configuration error",
    [SNPY_EINVPLUG - SNPY_EBASE] = "snappy - invalid plugin param",
    [SNPY_EAMBIPLUG - SNPY_EBASE] = "snappy - ambiguous plugin choice",
    [SNPY_ENOPLUG - SNPY_EBASE] = "snappy - no plugin found for the job",
    [SNPY_EINCOMPARG - SNPY_EBASE] = "snappy - incomplete argument",
    [SNPY_ELOG - SNPY_EBASE] = "snappy - log processing error",
    [SNPY_ERESPOOLFUL - SNPY_EBASE] = "snappy - resource pool full"
};

const char*  snpy_strerror(int errnum);


#endif
