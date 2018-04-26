#pragma once

#include <string>

class Checksum {
private:
	unsigned int crc_table[256];
public:
	Checksum();
	unsigned int CRC32(unsigned char *buf, size_t len);
};