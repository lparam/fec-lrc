#include <stdint.h>

int
read_size(uint8_t *buffer) {
	int r = (int)buffer[0] << 8 | (int)buffer[1];
	return r;
}

void
write_size(uint8_t *buffer, int len) {
	buffer[0] = (len >> 8) & 0xff;
	buffer[1] = len & 0xff;
}

uint32_t
read_size_32(uint8_t *buffer) {
    uint32_t r = buffer[0] | buffer[1] << 8 | buffer[2] << 16 | buffer[3] << 24;
    return r;
}

void
write_size_32(uint8_t *buffer, uint32_t len) {
	buffer[0] = len  & 0xff;
	buffer[1] = (len >> 8) & 0xff;
	buffer[2] = (len >> 16) & 0xff;
	buffer[3] = (len >> 24) & 0xff;
}
