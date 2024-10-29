/*
 * file.h
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>

#define FILE_NAME_MAX_LEN 255

struct FileHeader {
    uint8_t name_length;
    char file_name[FILE_NAME_MAX_LEN];
    uint32_t file_size;
};

class File {
public:
    File(const char* name, const unsigned char* file_data, uint32_t file_size);
    ~File();

    unsigned char* to_buff() const;
    static File from_buff(const unsigned char* buff);

    const unsigned char* get_data() const { return data; }
    uint32_t get_size() const { return header.file_size; }
    FileHeader get_header() const { return header; };

private:
    FileHeader header;
    unsigned char* data;
};
