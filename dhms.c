#include "dhms.h"

#include <stdio.h>

// 如果已经有相同的pool name，就返回这个pool
struct dhms_pool *dhms_create(const char *conf_file, const char *name) {
  fprintf(stderr, "[%s] conf file: %s, name: %s\n", __func__, conf_file, name);
  return NULL;
}
void dhms_destory(const char *name) {
  fprintf(stderr, "[%s] \n", __func__);
}
dhms_addr dhms_alloc(struct dhms_pool *pool, size_t size) {
  fprintf(stderr, "[%s] \n", __func__);
  static dhms_addr addr = 0UL;
  return addr++;
}
// 对于已申请但从未写入的addr，依然允许读取（返回脏值）
int dhms_read(struct dhms_pool *pool, dhms_addr addr, void *buf, size_t len) {
  fprintf(stderr, "[%s] Read Addr: (%#lx, %lu), Buf: (%p)\n", __func__, addr,
          len, buf);
  return len;
}
int dhms_write(struct dhms_pool *pool, dhms_addr addr, void *buf, size_t len) {
  fprintf(stderr, "[%s] Write Addr: %#lx, Buf: (%p, %lu)\n", __func__, addr,
          buf, len);
  return len;
}
void dhms_free(struct dhms_pool *pool, dhms_addr addr) {
  fprintf(stderr, "[%s] \n", __func__);
}