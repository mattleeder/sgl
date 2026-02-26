#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

static uint64_t read_big_endian(const uint8_t *data, size_t offset, size_t num_bytes) {
    uint64_t value = 0;
    for (size_t i = 0; i < num_bytes; i++) {
        value = (value << 8) | data[offset + i];
    }
    return value;
}

uint16_t read_u16_big_endian(const uint8_t *data, size_t offset) {
    return (uint16_t)read_big_endian(data, offset, 2);
}

uint32_t read_u24_big_endian(const uint8_t *data, size_t offset) {
    return (uint32_t)read_big_endian(data, offset, 3);
}

uint32_t read_u32_big_endian(const uint8_t *data, size_t offset) {
    return (uint32_t)read_big_endian(data, offset, 4);
}

uint64_t read_u48_big_endian(const uint8_t *data, size_t offset) {
    return read_big_endian(data, offset, 6);
}

uint64_t read_u64_big_endian(const uint8_t *data, size_t offset) {
    return read_big_endian(data, offset, 8);
}

uint64_t read_varint(const uint8_t *data, uint64_t *bytes_read) {
    /*
    A variable-length integer or "varint" is a static Huffman encoding of 
    64-bit twos-complement integers that uses less space for small positive values. 
    A varint is between 1 and 9 bytes in length. The varint consists of either zero 
    or more bytes which have the high-order bit set followed by a single byte with the 
    high-order bit clear, or nine bytes, whichever is shorter. The lower seven bits of 
    each of the first eight bytes and all 8 bits of the ninth byte are used to reconstruct 
    the 64-bit twos-complement integer. Varints are big-endian: bits taken from the earlier 
    byte of the varint are more significant than bits taken from the later bytes. 
    */

    // Bytes read is only incremented
    // Bytes read is used as an offset so repeated calls not need to 
    // add the previous bytes_read on
    //
    // e.g
    // uint64_t bytes_read = 0;
    // read_varint(data, &bytes_read); -> starts reading from data + 0, reads 6 bytes
    // read_varint(data, &bytes_read); -> starts reading from data + 6, reads 5 bytes
    // read_varint(data, &bytes_read); -> starts reading from data + 11, reads 7 bytes
    // bytes_read == 18

    uint64_t varint = 0;
    uint8_t byte;

    if (bytes_read == NULL) {
        uint64_t dummy = 0;
        bytes_read = &dummy;
    }


    bool varint_read = false;
    for (int i = 0; i < 8; i++) {
        byte = data[*bytes_read];
        (*bytes_read)++;

        // Remove high order bit
        varint = (varint << 7) | (byte & 0x7F);

        // If high order bit clear then this is last byte
        if (!(byte & 0x80)) {
            varint_read = true;
            break;
        }
    }

    if (!varint_read) {
        byte = data[*bytes_read];
        (*bytes_read)++;
        
        // Final byte uses all 8 bits
        varint = (varint << 8) | byte; 
    }

    return varint;
}