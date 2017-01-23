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
#include <errno.h>

#include "snpy_util.h"
#include "error.h"
#include "json.h"


const char* snpy_strerror(int errnum) {
    /*
    if (!buf) 
        return -EINVAL;
    buf[0] = 0;
    */
    if (errnum >= JSON_EBASE && errnum < JSON_ELAST) 
        return json_strerror(errnum);
    
    if (errnum >= SNPY_EBASE && errnum < SNPY_ELAST) 
        return snpy_errmsg_tab[errnum - SNPY_EBASE];
    else if (errnum < sys_nerr)
        return sys_errlist[errnum];
    else 
        return "unknown error";
}


