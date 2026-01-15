#ifndef PSRC_CRC_H
#define PSRC_CRC_H

unsigned crc32(const void*, long unsigned);
unsigned strcrc32(const char*);
unsigned strcasecrc32(const char*);
unsigned strncasecrc32(const char*, long unsigned);
unsigned ccrc32(unsigned, const void*, long unsigned);
unsigned cstrcrc32(unsigned, const char*);
unsigned cstrcasecrc32(unsigned, const char*);
unsigned cstrncasecrc32(unsigned, const char*, long unsigned);

#endif
