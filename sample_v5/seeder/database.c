#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include "database.h"
#include "meta.h" // For FileMetadata struct and reading
// #include "metadata.h"  // If you had a separate header for read_metadata()

#define RECORDS_FOLDER "records" // Directory containing .meta files
#define META_LOG_FILE "meta.log" // The meta log file


/* Function to read a single .meta file into a FileMetadata struct */
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

/* Function to add a new file mapping to meta.log */
void add_file_entry(ssize_t fileID, ssize_t totalBytes,const char *existingMetaFilename)
{
    FILE *dbFile = fopen(META_LOG_FILE, "ab"); // Append in binary mode
    if (!dbFile)
    {
        perror("Error opening meta.log for writing");
        exit(EXIT_FAILURE);
    }

    FileEntry entry;
    entry.fileID = fileID;

    // Here, we assume the file is already named e.g. "0001_gray_cat.png.meta"
    // so we just store that name in meta.log
    strncpy(entry.metaFilename, existingMetaFilename, sizeof(entry.metaFilename));
    entry.metaFilename[sizeof(entry.metaFilename) - 1] = '\0';
    entry.totalBytes = totalBytes;

    fwrite(&entry, sizeof(FileEntry), 1, dbFile);
    fclose(dbFile);

    printf("Added fileID: %04zd -> %s to meta.log\n", fileID, entry.metaFilename);
}

/* Function to list all file mappings from meta.log */
void list_file_entries()
{
    FILE *dbFile = fopen(META_LOG_FILE, "rb");
    if (!dbFile)
    {
        perror("Error opening meta.log for reading");
        exit(EXIT_FAILURE);
    }

    FileEntry entry;
    printf("\nCurrent Database Entries:\n");
    printf("---------------------------\n");

    while (fread(&entry, sizeof(FileEntry), 1, dbFile))
    {
        printf("File ID: %04zd, Total Bytes : %zd Meta File: %s\n", entry.fileID, entry.totalBytes, entry.metaFilename);
    }

    fclose(dbFile);
}

/* Function to look up a meta file name by fileID */
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

/* Load all file entries into a dynamically allocated array */
FileEntry *load_file_entries(size_t *outCount)
{
    FILE *dbFile = fopen(META_LOG_FILE, "rb");
    if (!dbFile)
    {
        perror("Error opening meta.log for loading entries");
        return NULL;
    }

    // Get total number of entries
    fseek(dbFile, 0, SEEK_END);
    size_t file_size = ftell(dbFile);
    size_t num_entries = file_size / sizeof(FileEntry);
    fseek(dbFile, 0, SEEK_SET);

    if (num_entries == 0)
    {
        fclose(dbFile);
        return NULL; // No entries found
    }

    // Allocate memory for the list
    FileEntry *entries = malloc(num_entries * sizeof(FileEntry));
    if (!entries)
    {
        perror("Memory allocation failed");
        fclose(dbFile);
        return NULL;
    }

    // Read all entries into memory
    fread(entries, sizeof(FileEntry), num_entries, dbFile);
    fclose(dbFile);

    *outCount = num_entries; // Store the number of entries
    return entries;
}

/*
   Function to scan the `records/` folder for .meta files,
   read the "fileID" from each metadata struct,
   and add an entry to meta.log
*/
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
        // Skip `.` and `..`
        if (entry->d_name[0] == '.')
            continue;

        // Check if the file has a .meta extension
        if (strstr(entry->d_name, ".meta") != NULL)
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", RECORDS_FOLDER, entry->d_name);

            // 1) Read the metadata from the file
            FileMetadata metadata;
            if (load_single_metadata(path, &metadata) == 0)
            {
                // 2) We have metadata.fileID, metadata.filename
                //    The file is already named e.g. "0001_gray_cat.png.meta"
                //    if you used the other code that created it that way.

                // 3) Add an entry to meta.log
                add_file_entry(metadata.fileID, metadata.totalByte, entry->d_name);
            }
        }
    }

    closedir(dir);
}



    // /* Main function: demonstration */
    // int main()
    // {
    //     // 1) Remove previous meta.log (for testing)
    //     remove(META_LOG_FILE);

    //     // 2) We assume you already put e.g. "0001_gray_cat.png.meta" inside `./records/`
    //     //    or you used the "metadata.c" -> test_meta() to create it.

    //     // 3) Now we scan the records folder
    //     scan_and_add_files();

    //     // 4) List all entries
    //     list_file_entries();

    //     // 5) Test lookup
    //     ssize_t testFileID = 1; // Suppose we have fileID=1
    //     char *filename = get_meta_filename(testFileID);
    //     if (filename)
    //     {
    //         printf("\nLookup: File ID %04zd -> Meta File: %s\n", testFileID, filename);
    //         free(filename);
    //     }
    //     else
    //     {
    //         printf("\nLookup: File ID %04zd not found\n", testFileID);
    //     }

    //     // 6) Load all file entries into an array
    //     size_t fileCount = 0;
    //     FileEntry *entries = load_file_entries(&fileCount);
    //     if (entries)
    //     {
    //         printf("\nLoading metadata for each file entry...\n");

    //         for (size_t i = 0; i < fileCount; i++) // Use fileCount instead of g_fileCount
    //         {
    //             char fullPath[512];
    //             snprintf(fullPath, sizeof(fullPath), "./records/%s", entries[i].metaFilename);

    //             FileMetadata metadata;
    //             if (load_single_metadata(fullPath, &metadata) == 0)
    //             {
    //                 printf("File ID: %04zd\n", metadata.fileID);
    //                 printf("Filename: %s\n", metadata.filename);
    //                 printf("Total Size: %zd bytes\n", metadata.totalByte);
    //                 printf("Total Chunks: %zd\n", metadata.totalChunk);
    //                 printf("-----------------------------\n");
    //             }
    //             else
    //             {
    //                 printf("Failed to read metadata for: %s\n", entries[i].metaFilename);
    //             }
    //         }

    //         free(entries);
    //     }
    //     else
    //     {
    //         printf("\nNo entries found in meta.log\n");
    //     }

    //     return 0;
    // }
