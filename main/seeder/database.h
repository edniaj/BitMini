#ifndef DATABASE_H
#define DATABASE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include "meta.h"
#define RECORDS_FOLDER "records" // Directory containing .meta files
#define META_LOG_FILE "meta.log" // The meta log file

/* Define a struct to store file mappings */
typedef struct
{   
    ssize_t fileID;
    ssize_t totalBytes;
    char metaFilename[256]; // Store the filename relative to records/
} FileEntry;
ssize_t add_new_file(const FileMetadata *meta);

/* Function to add a new file mapping to meta.log */
void add_file_entry(ssize_t fileID, ssize_t totalBytes, const char *metaFilename);

/* Function to list all file mappings from meta.log */
void list_file_entries();

/* Function to look up a meta file name by fileID */
char *get_meta_filename(ssize_t fileID);

/* Function to load all file entries into a dynamically allocated array */
FileEntry *load_file_entries(size_t *outcount);

/* Function to scan the `records/` folder and add all .meta files */
void scan_and_add_files();
int load_single_metadata(const char *filepath, FileMetadata *metadata);

#endif // DATABASE_H
