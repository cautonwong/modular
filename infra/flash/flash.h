#ifndef INFRA_FLASH_H
#define INFRA_FLASH_H

#include <stddef.h>
#include <stdint.h>

int flash_read(void *self, uint32_t off, void *buf, size_t len);
int flash_write(void *self, uint32_t off, const void *buf, size_t len);

#endif
