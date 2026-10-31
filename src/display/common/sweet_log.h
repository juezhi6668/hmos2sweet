/* Minimal hilog wrapper for the sweet display HALs (links against libhilog.so). */
#ifndef SWEET_LOG_H
#define SWEET_LOG_H
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __cplusplus
extern "C" {
#endif
int HiLogPrint(int type, int level, unsigned int domain, const char *tag, const char *fmt, ...)
    __attribute__((__format__(os_log, 5, 6)));
#ifdef __cplusplus
}
#endif
#define SWEET_LOG_DOMAIN 0xD002590
#ifndef SWEET_LOG_TAG
#define SWEET_LOG_TAG "SweetHAL"
#endif
/* LOG_CORE = 3, levels: DEBUG 3, INFO 4, WARN 5, ERROR 6 */
#define SLOGI(fmt, ...) HiLogPrint(3, 4, SWEET_LOG_DOMAIN, SWEET_LOG_TAG, "%{public}s: " fmt, __func__, ##__VA_ARGS__)
#define SLOGW(fmt, ...) HiLogPrint(3, 5, SWEET_LOG_DOMAIN, SWEET_LOG_TAG, "%{public}s: " fmt, __func__, ##__VA_ARGS__)
#define SLOGE(fmt, ...) HiLogPrint(3, 6, SWEET_LOG_DOMAIN, SWEET_LOG_TAG, "%{public}s: " fmt, __func__, ##__VA_ARGS__)

/* hilog lines never reach the boot log we can pull off the device, so the
 * allocator mirrors its decisions into /dev/kmsg as well (init chmods it 0666;
 * O_WRONLY needs no capability, only file permission). */
static inline void SweetKlog(const char *fmt, ...) __attribute__((__format__(printf, 1, 2)));
static inline void SweetKlog(const char *fmt, ...)
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "<14>hdibu: ");
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - (size_t)n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 2) {
        n = (int)sizeof(buf) - 2;
    }
    buf[n++] = '\n';
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t w = write(fd, buf, (size_t)n);
        (void)w;
        close(fd);
    }
}
#define SKLOG SweetKlog
#endif
