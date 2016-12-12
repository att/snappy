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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "util.h"

/* mempcpy() - same as GNU mempcpy function
*/
void *
mempcpy (void *dest, const void *src, size_t n)
{
  return (char *) memcpy (dest, src, n) + n;
}
#if 0
int kv_get_val(const char *key, char *val, int val_size) 
{
    int fd;
    int status = 0;
    struct stat sb;

 
    if ((fd = open(key, O_RDONLY)) == -1)
        return -errno;
    if (fstat(fd, &sb)) {
        status = errno;
        goto close_fd;
    }
    if (sb.st_size >= val_size) {
        status = ERANGE;
        goto close_fd;
    }

    int nbyte = read(fd, val, sb.st_size); 
    if (nbyte != sb.st_size) {
        status = errno;
        goto close_fd;
    }
    val[nbyte] = 0;
    char *p; (p = strchr(val, '\n')) &&  (*p = 0);
    return 0;

close_fd:
    close(fd);
    return -status;
}

int kv_put_val(const char *key, const char *val, int val_size) 
{
    int val_len = strnlen(val, val_size);
    int fd;


    if (val_len >= val_size)
        goto err_out;
    if ((fd = open(key, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0)
        goto err_out;
    if (write(fd, val, val_len) != val_len)
        goto close_fd;
    return 0;
close_fd:
        close(fd);
err_out:
        return 1;
}
#endif

/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t
strlcat(char *dst, const char *src, size_t siz)
{
    register char *d = dst;
    register const char *s = src;
    register size_t n = siz;
    size_t dlen;

    /* Find the end of dst and adjust bytes left but don't go past end */
    while (n-- != 0 && *d != '\0')
        d++;
    dlen = d - dst;
    n = siz - dlen;

    if (n == 0)
        return(dlen + strlen(s));
    while (*s != '\0') {
        if (n != 1) {
            *d++ = *s;
            n--;
        }
        s++;
    }
    *d = '\0';

    return(dlen + (s - src));   /* count does not include NUL */
}

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
    register char *d = dst;
    register const char *s = src;
    register size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0) {
        do {
            if ((*d++ = *s++) == 0)
                break;
        } while (--n != 0);
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';      /* NUL-terminate dst */
        while (*s++)
            ;
    }

    return(s - src - 1);    /* count does not include NUL */
}
