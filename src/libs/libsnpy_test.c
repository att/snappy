#include "snpy_util.h"
#include "snpy_log.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>


int main(int argc, char *argv[]) {
    
    const char *path = "/var/lib/snappy";

    if (argc == 2) 
        path = argv[1];
    ssize_t free_space = snpy_get_free_spc(path);
    if (free_space < 0) {
        fprintf(stderr, "%s", strerror(-free_space));
    }
    
    printf("free space: %zu\n", free_space);

    printf("free memort: %zu\n", snpy_get_free_mem());

    double loadavg[3];
    getloadavg(loadavg, ARRAY_SIZE(loadavg));

    /*
    for (i = 0; i < sizeof loadavg; i ++) {
        printf("%lf ", loadavg[i]);
    }
    */
    
    const char *str_tab[] = {
        "foo",
        "bar",
        "baz"
    };
    long long a[] = {111111111111, 22222222222222,3333333333332323};
    SNPY_PRINT_ARRAY(loadavg, ARRAY_SIZE(loadavg), "%lf", "\t");
    SNPY_PRINT_ARRAY(str_tab, ARRAY_SIZE(str_tab), "%s", "\t");
    SNPY_PRINT_ARRAY(a, ARRAY_SIZE(a), "%lld", "\t");
    int i;
    /* gcc statement expr test */
    printf("%f\n", ({double tmp; getloadavg(&tmp, 1); tmp;}));

    
    /* test snpy_log */
    struct snpy_log log;
    int r;
    r = snpy_log_open(&log, "/tmp/log_test.log", 0);
    assert(r == 0);
    r = snpy_log(&log, SNPY_LOG_INFO, "test snpy_log.", "%d-%d", 123, 123);
    snpy_log_close(&log);
    return 0;
}
