#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include "database.h"
#include "meta.h" // For FileMetadata struct and reading

// Directory and file definitions
#define RECORDS_FOLDER "records" 
#define META_LOG_FILE  "meta.log"

// --------------------------------------------------------
// Forward Declarations
// --------------------------------------------------------
static ssize_t get_next_available_fileID(void);

// --------------------------------------------------------
//  1) add_new_file() 
//     Creates a new fileID, writes the .meta to "records/", 
//     and appends an entry to meta.log. Returns the fileID.
//     On error, returns -1.
// --------------------------------------------------------
ssize_t add_new_file(const FileMetadata *meta)
{
    // 1) Figure out the next available fileID
    ssize_t newID = get_next_available_fileID();
    if (newID < 1) {
        fprintf(stderr, "Failed to get a valid fileID.\n");
        return -1;
    }

    // 2) Construct a filename like "0005_originalname.meta"
    //    or "0005_filename.png.meta" – your choice. 
    //    Below we show:  0005_<meta->filename>.meta
    char newFilename[512];
    snprintf(newFilename, sizeof(newFilename), 
             "%04zd_%s.meta", newID, meta->filename);

    // 3) Full path in the records folder
    char fullPath[512];
    int written = snprintf(fullPath, sizeof(fullPath),
                           "%s/%s", RECORDS_FOLDER, newFilename);
    if (written < 0 || written >= (int)sizeof(fullPath)) {
        fprintf(stderr, "Path truncated or error in snprintf!\n");
        return -1;
    }

    // 4) Write the FileMetadata to "records/0005_filename.meta"
    FILE *fp = fopen(fullPath, "wb");
    if (!fp) {
        perror("Error opening new .meta file in records/");
        return -1;
    }

    // We'll make a temp copy so we can assign the newID in the struct
    FileMetadata temp = *meta;
    temp.fileID = newID;

    if (fwrite(&temp, sizeof(FileMetadata), 1, fp) != 1) {
        perror("Error writing FileMetadata to file");
        fclose(fp);
        return -1;
    }
    fclose(fp);

    // 5) Append an entry to meta.log
    //    (This function is from your existing code below.)
    add_file_entry(newID, temp.totalByte, newFilename);

    // 6) Return the newly assigned fileID
    return newID;
}

// --------------------------------------------------------
//  2) get_next_available_fileID() 
//     Scans meta.log for the largest fileID in use, returns maxID+1
// --------------------------------------------------------
static ssize_t get_next_available_fileID(void)
{
    FILE *dbFile = fopen(META_LOG_FILE, "rb");
    if (!dbFile) {
        // If meta.log doesn't exist, start at 1
        return 1;
    }

    ssize_t maxID = 0;
    FileEntry entry;
    while (fread(&entry, sizeof(FileEntry), 1, dbFile) == 1)
    {
        if (entry.fileID > maxID) {
            maxID = entry.fileID;
        }
    }

    fclose(dbFile);
    return maxID + 1;  // Next unused
}

// --------------------------------------------------------
//  3) load_single_metadata()
//     Reads one .meta file into FileMetadata struct
// --------------------------------------------------------
int load_single_metadata(const char *filepath, FileMetadata *out)
{
    FILE *fp = fopen(filepath, "rb");
    if (!fp)
    {
        perror("Error opening .meta file for reading");
        return -1;
    }
    if (fread(out, sizeof(FileMetadata), 1, fp) != 1)
    {
        perror("Error reading .meta file into FileMetadata struct");
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

// --------------------------------------------------------
//  4) add_file_entry()
//     Appends an entry to meta.log
// --------------------------------------------------------
void add_file_entry(ssize_t fileID, ssize_t totalBytes,
                    const char *existingMetaFilename)
{
    FILE *dbFile = fopen(META_LOG_FILE, "ab"); // Append in binary mode
    if (!dbFile)
    {
        perror("Error opening meta.log for writing");
        exit(EXIT_FAILURE);
    }

    FileEntry entry;
    entry.fileID = fileID;
    strncpy(entry.metaFilename, existingMetaFilename,
            sizeof(entry.metaFilename));
    entry.metaFilename[sizeof(entry.metaFilename) - 1] = '\0';
    entry.totalBytes = totalBytes;

    fwrite(&entry, sizeof(FileEntry), 1, dbFile);
    fclose(dbFile);

    printf("Added fileID: %04zd -> %s to meta.log\n",
           fileID, entry.metaFilename);
}

// --------------------------------------------------------
//  5) list_file_entries()
//     Reads meta.log and prints all FileEntries
// --------------------------------------------------------
void list_file_entries()
{
    FILE *dbFile = fopen(META_LOG_FILE, "rb");
    if (!dbFile)
    {
        perror("Error opening meta.log for reading");
        exit(EXIT_FAILURE);
    }

    FileEntry entry;
    memset(&entry, 0, sizeof(FileEntry));

    printf("\nCurrent Database Entries:\n");
    printf("---------------------------\n");

    while (fread(&entry, sizeof(FileEntry), 1, dbFile))
    {
        printf("File ID: %04zd, Total Bytes: %zd, Meta File: %s\n",
               entry.fileID, entry.totalBytes, entry.metaFilename);
    }

    fclose(dbFile);
}

// --------------------------------------------------------
//  6) get_meta_filename()
//     Looks up meta.log for a matching fileID, returns a malloc'ed 
//     copy of the metaFilename, or NULL if not found
// --------------------------------------------------------
char *get_meta_filename(ssize_t fileID)
{
    FILE *dbFile = fopen(META_LOG_FILE, "rb");
    if (!dbFile)
    {
        perror("Error opening meta.log for lookup");
        return NULL;
    }

    FileEntry entry;
    while (fread(&entry, sizeof(FileEntry), 1, dbFile))
    {
        if (entry.fileID == fileID)
        {
            fclose(dbFile);
            // Allocate space for the filename
            char *filename = malloc(strlen(entry.metaFilename) + 1);
            if (filename)
            {
                strcpy(filename, entry.metaFilename);
            }
            return filename;
        }
    }

    fclose(dbFile);
    return NULL; // Not found
}

// --------------------------------------------------------
//  7) load_file_entries()
//     Reads all FileEntry records from meta.log, returns 
//     a dynamically allocated array of them + the count
// --------------------------------------------------------
FileEntry *load_file_entries(size_t *outCount)
{
    FILE *dbFile = fopen(META_LOG_FILE, "rb");
    if (!dbFile)
    {
        perror("Error opening meta.log for loading entries");
        return NULL;
    }

    // Determine how many entries are in the file
    fseek(dbFile, 0, SEEK_END);
    size_t file_size = ftell(dbFile);
    size_t num_entries = file_size / sizeof(FileEntry);
    fseek(dbFile, 0, SEEK_SET);

    if (num_entries == 0)
    {
        fclose(dbFile);
        *outCount = 0;
        return NULL; // No entries found
    }

    // Allocate memory for the array
    FileEntry *entries = malloc(num_entries * sizeof(FileEntry));
    if (!entries)
    {
        perror("Memory allocation failed");
        fclose(dbFile);
        return NULL;
    }

    // Read them all into the array
    if (fread(entries, sizeof(FileEntry), num_entries, dbFile) != num_entries)
    {
        perror("Error reading file entries from meta.log");
        free(entries);
        fclose(dbFile);
        return NULL;
    }

    fclose(dbFile);
    *outCount = num_entries;
    return entries;
}


void scan_and_add_files()
{
    DIR *dir = opendir(RECORDS_FOLDER);
    if (!dir)
    {
        perror("Error opening records directory");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // Skip '.' and '..'
        if (entry->d_name[0] == '.')
            continue;

        // Check if file has a .meta extension
        if (strstr(entry->d_name, ".meta") != NULL)
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", RECORDS_FOLDER, entry->d_name);

            // 1) Read the metadata from the file
            FileMetadata metadata;
            if (load_single_metadata(path, &metadata) == 0)
            {
                // 2) Add an entry to meta.log (if not already present)
                //    Or maybe you check if fileID is already in the log, etc.
                printf("Found fileID %zd, totalByte %zd, name: %s\n",
                       metadata.fileID, metadata.totalByte, entry->d_name);

                add_file_entry(metadata.fileID, metadata.totalByte, entry->d_name);
            }
        }
    }

    closedir(dir);
}

// int main() {
//     FILE *fp = fopen(META_LOG_FILE, "wb"); // 'wb' creates/truncates the file
//     if (!fp) {
//         perror("Failed to create empty meta.log");
//         return EXIT_FAILURE;
//     }

//     printf("✅ Created an empty meta.log file.\n");
//     fclose(fp);
//     return EXIT_SUCCESS;
// }