/*
 * file.cpp
 */

#include "file.h"

File::File(const char* name, const unsigned char* file_data, uint32_t file_size)
{
    // Name length
    header.name_length = static_cast<uint8_t>(std::strlen(name));

    // File name
    std::strncpy(header.file_name, name, FILE_NAME_MAX_LEN);

    // File sie
    header.file_size = file_size;

    // Data
    data = new unsigned char[file_size];
    std::memcpy(data, file_data, file_size);
}

File::~File()
{
    delete[] data;
}

unsigned char* File::to_buff() const
{
    size_t total_size = sizeof(header.name_length) + header.name_length + sizeof(header.file_size) + header.file_size;
    unsigned char* buffer = new unsigned char[total_size];

    unsigned char* ptr = buffer;

    // Name length
    std::memcpy(ptr, &header.name_length, sizeof(header.name_length));
    ptr += sizeof(header.name_length);

    // File name
    std::memcpy(ptr, header.file_name, header.name_length);
    ptr += header.name_length;

    // File size
    std::memcpy(ptr, &header.file_size, sizeof(header.file_size));
    ptr += sizeof(header.file_size);

    // Data
    std::memcpy(ptr, data, header.file_size);

    return buffer;
}


File File::from_buff(const unsigned char* buff)
{
    const unsigned char* ptr = buff;

    // Name length
    uint8_t name_length;
    std::memcpy(&name_length, ptr, sizeof(name_length));
    ptr += sizeof(name_length);

    // File name
    char file_name[FILE_NAME_MAX_LEN + 1];
    std::memcpy(file_name, ptr, name_length);
    file_name[name_length] = '\0';
    ptr += name_length;

    // File size
    uint32_t size;
    std::memcpy(&size, ptr, sizeof(size));
    ptr += sizeof(size);

    // Data
    unsigned char* file_data = new unsigned char[size];
    std::memcpy(file_data, ptr, size);

    return File(file_name, file_data, size);
}
