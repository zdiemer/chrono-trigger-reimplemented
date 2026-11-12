#ifndef CT_CHECKSUM_H
#define CT_CHECKSUM_H
#include <stddef.h>
#include <stdint.h>
uint32_t ct_crc32(const uint8_t *data, size_t len);
#endif
