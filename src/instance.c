#include "instance.h"

#ifndef PS5

void instance_take_over(void) {}

#else

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define PROCESS_NAME "prospero-gallery"

// Walks the kernel's process list looking for another process with our name.
// The PS5's process records don't match FreeBSD's headers, so the fields are
// read at fixed offsets (the same ones the SDK's test_privileges sample uses).
static pid_t find_other(const char *name)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t size;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0) return -1;

    uint8_t *buf = malloc(size);
    if (buf == NULL) return -1;
    if (sysctl(mib, 4, buf, &size, NULL, 0) != 0) {
        free(buf);
        return -1;
    }

    pid_t me = getpid(), found = -1;
    for (uint8_t *p = buf; p < buf + size; ) {
        int record_size = *(int *)p;
        pid_t pid = *(pid_t *)&p[72];
        const char *thread_name = (const char *)&p[447];
        if (record_size <= 0) break;
        if (pid != me && strcmp(thread_name, name) == 0) found = pid;
        p += record_size;
    }
    free(buf);
    return found;
}

void instance_take_over(void)
{
    syscall(SYS_thr_set_name, -1, PROCESS_NAME);

    pid_t old;
    for (int tries = 0; tries < 5 && (old = find_other(PROCESS_NAME)) > 0; tries++) {
        kill(old, SIGKILL);
        sleep(1);   // give the kernel a moment to free its port
    }
}

#endif
