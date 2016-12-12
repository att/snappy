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

#ifndef CEPH_MEMORY_H
#define CEPH_MEMORY_H

#include <ciso646>

#ifdef _LIBCPP_VERSION

#include <memory>

namespace ceph {
  using std::shared_ptr;
  using std::weak_ptr;
}

#else

#include <tr1/memory>

namespace ceph {
  using std::tr1::shared_ptr;
  using std::tr1::weak_ptr;
}

#endif

#endif
