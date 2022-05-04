#ifndef __DHMS_H__
#define __DHMS_H__

#include <stdint.h>
#include <stdlib.h>

typedef uint64_t dhms_addr;
struct dhms_pool {};
struct dhms_pool *dhms_create(const char* conf_file, const char *name);
void dhms_destory(const char *name);
dhms_addr dhms_alloc(struct dhms_pool *pool, size_t size);
int dhms_read(struct dhms_pool *pool, dhms_addr addr, void *buf, size_t len);
int dhms_write(struct dhms_pool *pool, dhms_addr addr, void *buf, size_t len);
void dhms_free(struct dhms_pool *pool, dhms_addr addr);

#endif // __DHMS_H__