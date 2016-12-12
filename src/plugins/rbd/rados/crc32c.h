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

#ifndef CEPH_CRC32C_H
#define CEPH_CRC32C_H

#include <inttypes.h>
#include <string.h>

typedef uint32_t (*ceph_crc32c_func_t)(uint32_t crc, unsigned char const *data, unsigned length);

/*
 * this is a static global with the chosen crc32c implementation for
 * the given architecture.
 */
extern ceph_crc32c_func_t ceph_crc32c_func;

extern ceph_crc32c_func_t ceph_choose_crc32(void);

/**
 * calculate crc32c
 *
 * Note: if the data pointer is NULL, we calculate a crc value as if
 * it were zero-filled.
 *
 * @param crc initial value
 * @param data pointer to data buffer
 * @param length length of buffer
 */
static inline uint32_t ceph_crc32c(uint32_t crc, unsigned char const *data, unsigned length)
{
	return ceph_crc32c_func(crc, data, length);
}

#endif
