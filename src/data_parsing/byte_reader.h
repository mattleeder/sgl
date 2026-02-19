#ifndef sql_byte_reader
#define sql_byte_reader

#include <stdint.h>
#include <stdbool.h>

uint16_t read_u16_big_endian(const uint8_t *data, size_t offset);
uint32_t read_u24_big_endian(const uint8_t *data, size_t offset);
uint32_t read_u32_big_endian(const uint8_t *data, size_t offset);
uint64_t read_u48_big_endian(const uint8_t *data, size_t offset);
uint64_t read_u64_big_endian(const uint8_t *data, size_t offset);
uint64_t read_varint(const uint8_t *data, uint64_t *bytes_read);

#endif