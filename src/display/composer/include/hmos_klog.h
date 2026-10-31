/*
 * Kernel-log helper for the composer bring-up diagnostics.
 *
 * hilog flow control drops composer lines while the system is busy, so the
 * interesting state (device nodes, pipe selection, commit return codes) also
 * goes to /dev/kmsg where it survives in the boot log.
 */
#ifndef HMOS_KLOG_H
#define HMOS_KLOG_H
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include "display_log.h"

namespace OHOS {
namespace HDI {
namespace DISPLAY {
static inline void HmosKlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static inline void HmosKlog(const char *fmt, ...)
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "<14>");
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
    va_end(ap);
    if (n > static_cast<int>(sizeof(buf)) - 2) {
        n = sizeof(buf) - 2;
    }
    buf[n++] = '\n';
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t w = write(fd, buf, n);
        (void)w;
        close(fd);
    }
    DISPLAY_LOGI("%{public}s", buf + 4);
}
} // namespace DISPLAY
} // namespace HDI
} // namespace OHOS

#endif // HMOS_KLOG_H
