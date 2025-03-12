#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>

#define RECORDS_FOLDER "records" // Directory containing .meta files
#define META_LOG_FILE "meta.log" // The meta log file

/* Define a struct to store file mappings */
typedef struct
{
    ssize_t fileID;
    char metaFilename[256]; // Store the filename relative to records/
} FileEntry;

/* Function to add a new file mapping to meta.log */
void add_file_entry(ssize_t fileID, const char *metaFilename)
{
    FILE *dbFile = fopen(META_LOG_FILE, "ab"); // Append in binary mode
    if (!dbFile)
    {
        perror("Error opening meta.log for writing");
        exit(EXIT_FAILURE);
    }

    FileEntry entry;
    entry.fileID = fileID;
    snprintf(entry.metaFilename, sizeof(entry.metaFilename), "%s/%s", RECORDS_FOLDER, metaFilename);

    fwrite(&entry, sizeof(FileEntry), 1, dbFile);
    fclose(dbFile);

    printf("Added fileID: %zd -> %s to meta.log\n", fileID, entry.metaFilename);
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
        printf("File ID: %zd, Meta File: %s\n", entry.fileID, entry.metaFilename);
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
            strcpy(filename, entry.metaFilename);
            return filename;
        }
    }

    fclose(dbFile);
    return NULL; // Not found
}

/* ✅ NEW FUNCTION: Load all file entries into a dynamically allocated array */
FileEntry *load_file_entries()
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

    return entries; // Returns a dynamically allocated list of entries
}

/* Function to scan the `records/` folder and add all .meta files */
void scan_and_add_files()
{
    DIR *dir = opendir(RECORDS_FOLDER);
    if (!dir)
    {
        perror("Error opening records directory");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    ssize_t fileID = 1001; // Start fileID from 1001 (adjust as needed)

    while ((entry = readdir(dir)) != NULL)
    {
        // Skip `.` and `..`
        if (entry->d_name[0] == '.')
            continue;

        // Check if the file has a .meta extension
        if (strstr(entry->d_name, ".meta") != NULL)
        {
            add_file_entry(fileID, entry->d_name);
            fileID++; // Increment fileID for the next entry
        }
    }

    closedir(dir);
}

/* Main function: Run tests */
int main()
{
    // Remove previous meta.log (for testing)
    remove(META_LOG_FILE);

    // Scan the records folder and add entries
    scan_and_add_files();

    // List all entries
    list_file_entries();

    // Test lookup
    ssize_t testFileID = 1003;
    char *filename = get_meta_filename(testFileID);
    if (filename)
    {
        printf("\nLookup: File ID %zd -> Meta File: %s\n", testFileID, filename);
        free(filename);
    }
    else
    {
        printf("\nLookup: File ID %zd not found\n", testFileID);
    }

    // ✅ Load all file entries into an array
    FileEntry *entries = load_file_entries();

    if (entries)
    {
        printf("\nLoaded file entries into memory:\n");
        for (size_t i = 0; entries[i].fileID != 0; i++) // Assuming 0 is an invalid fileID
        {
            printf("File ID: %zd, Meta File: %s\n", entries[i].fileID, entries[i].metaFilename);
        }
        free(entries); // Free the allocated memory
    }
    else
    {
        printf("\nNo entries found in meta.log\n");
    }

    return 0;
}
