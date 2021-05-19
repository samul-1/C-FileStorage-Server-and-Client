#ifndef RLE_COMPRESSION_H
#define RLE_COMPRESSION_H

char* RLEcompress(char* data, size_t origSize, size_t* compressedSize);
char* RLEdecompress(char* data, size_t compressedSize, size_t uncompressedSize, size_t extraAllocation);

#endif