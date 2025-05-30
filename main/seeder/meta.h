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
// meta.h
char *generate_metafile_path_by_fileid(ssize_t fileID);
void create_metadata(const char *binary_filepath, FileMetadata *fileMetaData);
int write_metadata(const char *meta_filepath, const FileMetadata *fileMetaData);
void read_metadata(const char *meta_filename, FileMetadata *fileMetaData);
char *generate_metafile_filepath_with_id(ssize_t fileID, const char *binary_filepath);
#endif // META_H
