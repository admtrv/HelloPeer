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

    // File size
    header.file_size = file_size;

    // Data
    data = new unsigned char[file_size];
    std::memcpy(data, file_data, file_size);
}

File::File(const File& other)
{
    header = other.header;
    data = new unsigned char[header.file_size];
    std::memcpy(data, other.data, header.file_size);
}

File& File::operator=(const File& other)
{
    if (this != &other)
    {
        delete[] data;
        header = other.header;
        data = new unsigned char[header.file_size];
        std::memcpy(data, other.data, header.file_size);
    }
    return *this;
}

File::File(File&& other) noexcept : header(other.header), data(other.data)
{
    other.data = nullptr;
}

File& File::operator=(File&& other) noexcept
{
    if (this != &other)
    {
        delete[] data;
        header = other.header;
        data = other.data;
        other.data = nullptr;
    }
    return *this;
}

File::~File()
{
    if (data != nullptr)
    {
        delete[] data;
        data = nullptr;
    }
}

unsigned char* File::to_buff() const
{
    size_t total_size = sizeof(header.name_length) + header.name_length + sizeof(header.file_size) + header.file_size;
    auto* buffer = new unsigned char[total_size];

    unsigned char* ptr = buffer;

    // Name length
    std::memcpy(ptr, &header.name_length, sizeof(header.name_length));
    ptr += sizeof(header.name_length);

    // File name
    std::memcpy(ptr, header.file_name, header.name_length);
    ptr += header.name_length;

    // File size
    uint32_t file_size_net = htonl(header.file_size);
    std::memcpy(ptr, &file_size_net, sizeof(file_size_net));
    ptr += sizeof(file_size_net);

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
    uint32_t size_net;
    std::memcpy(&size_net, ptr, sizeof(size_net));
    ptr += sizeof(size_net);
    uint32_t size = ntohl(size_net);

    // Data
    auto file_data = new unsigned char[size];
    std::memcpy(file_data, ptr, size);

    return File(file_name, file_data, size);
}
