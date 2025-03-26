#ifndef META_H
#define META_H

#include <stdint.h>

// Define the struct
typedef struct FileMetaData
{
    char filename[128];
    ssize_t fileID;
    ssize_t totalByte;
    ssize_t totalChunk;
    uint8_t fileHash[32]; // SHA-256 hash
} FileMetadata;

// Function declarations
void create_metadata(char *binary_filepath, FileMetadata *fileMetaData);
void write_metadata(const char *meta_filepath, const FileMetadata *fileMetaData);
void read_metadata(const char *meta_filename);

#endif // META_H
