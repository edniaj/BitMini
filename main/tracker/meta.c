#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h> /* SHA-256 hash*/
#include "meta.h"
#include "database.h"
#define BUFFER_SIZE 1024 * 5
#define CHUNK_SIZE 1024

/*
metadata should be in json format - readability
filename should be given by User
*/


uint8_t* get_filehash_by_fileid(ssize_t fileID) {
    static uint8_t hash[32]; // Static buffer to return
    
    // Generate metadata file path
    char* filepath = build_metafile_path_by_fileid(fileID);
    if (!filepath) return NULL;
    
    // Open metadata file
    FILE* file = fopen(filepath, "rb");
    free(filepath); // Free the filepath memory
    
    if (!file) return NULL;
    
    // Read metadata structure
    FileMetadata metadata;
    if (fread(&metadata, sizeof(FileMetadata), 1, file) != 1) {
        fclose(file);
        return NULL;
    }
    
    // Copy file hash to our static buffer
    memcpy(hash, metadata.fileHash, 32);
    
    fclose(file);
    return hash;
}

char* build_metafile_path_by_fileid(ssize_t fileID)
{
    char *filename = malloc(256); // Allocate memory
    if (filename == NULL)
        return NULL;

    snprintf(filename, 256, "%s/%s", RECORDS_FOLDER, get_meta_filename(fileID));
    return filename;
}


char *generate_metafile_filepath_with_id(ssize_t fileID, const char *binary_filepath)
{
    // 1) Find the last slash in the path (if any).
    const char *slash = strrchr(binary_filepath, '/');

    // We'll store directory portion in dirPart, and the filename portion in baseName
    char dirPart[1024];
    char baseName[1024];

    if (slash)
    {
        // The directory is everything up to (and including) the slash
        size_t dirLen = (size_t)(slash - binary_filepath) + 1; // +1 to include slash itself
        if (dirLen >= sizeof(dirPart))
        {
            // Path too long for our buffer – handle error if needed
            return NULL;
        }
        memcpy(dirPart, binary_filepath, dirLen);
        dirPart[dirLen] = '\0';

        // The basename starts after the slash
        strncpy(baseName, slash + 1, sizeof(baseName) - 1);
        baseName[sizeof(baseName) - 1] = '\0';
    }
    else
    {
        // No slash found, so there's no directory portion
        dirPart[0] = '\0';
        // The entire binary_filepath is the base name
        strncpy(baseName, binary_filepath, sizeof(baseName) - 1);
        baseName[sizeof(baseName) - 1] = '\0';
    }

    // 2) Construct something like "./storage_downloads/0021_red_dog.png.meta"
    //    We need enough space for: directory + 4-digit fileID + underscore + basename + ".meta" + null terminator.
    size_t finalSize = strlen(dirPart)    // e.g. "./storage_downloads/"
                       + 5                // 4 digits for fileID + underscore (e.g. "0021_")
                       + strlen(baseName) // e.g. "red_dog.png"
                       + strlen(".meta")  // ".meta"
                       + 1;               // null terminator

    char *result = (char *)malloc(finalSize);
    if (!result)
        return NULL;

    snprintf(result, finalSize, "%s%04zd_%s.meta", // zero-pad fileID to 4 digits
             dirPart, fileID, baseName);

    return result; // caller must free!
}

char *generate_filename(const char *binary_filepath)
{
    // Find the last '/' character in the path
    const char *filename = strrchr(binary_filepath, '/');

    // If '/' is found, move one character ahead; otherwise, use the full path
    filename = filename ? filename + 1 : binary_filepath;

    // Allocate memory for the filename string
    char *result = malloc(strlen(filename) + 1);
    if (!result)
        return NULL;

    strcpy(result, filename);
    return result;
}

void create_metadata(const char *binary_filepath, FileMetadata *fileMetaData)
{
    if (!fileMetaData)
    {
        fprintf(stderr, "create_metadata: fileMetaData is NULL!\n");
        return;
    }

    int byte_read = 0, total_byte_read = 0;
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen(binary_filepath, "rb");

    if (!fp)
    {
        perror("Error opening file");
        return;
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    while ((byte_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0)
    {
        total_byte_read += byte_read;
        SHA256_Update(&sha256, buffer, byte_read);
    }

    fileMetaData->totalChunk = (total_byte_read + CHUNK_SIZE - 1) / CHUNK_SIZE;
    fileMetaData->totalByte = total_byte_read;
    fileMetaData->fileID = -1; // Unassigned
    SHA256_Final(fileMetaData->fileHash, &sha256);

    // ✅ Fix here
    char *fname = generate_filename(binary_filepath);
    if (fname)
    {
        strncpy(fileMetaData->filename, fname, sizeof(fileMetaData->filename) - 1);
        fileMetaData->filename[sizeof(fileMetaData->filename) - 1] = '\0';
        free(fname);
    }

    printf("Hashed successfully: ");
    for (int i = 0; i < 32; i++)
    {
        printf("%02x", fileMetaData->fileHash[i]);
    }
    printf("\n");

    fclose(fp);
}

/* Write out the FileMetadata in binary form */
int write_metadata(const char *meta_filepath, const FileMetadata *fileMetaData)
{
    // (Optional) If you want to ensure the directory exists automatically:
    //  e.g. create ./storage_metafile if that's in meta_filepath
    //  You need to parse out the directory portion from meta_filepath.
    //  For example:
    //
    //  mkdir("./storage_metafile", 0755);
    //  // If directory already exists, that's OK (EEXIST).
    //  // You'd handle that gracefully if you want to be robust.

    FILE *fp = fopen(meta_filepath, "wb");
    if (fp == NULL)
    {
        perror("ERROR opening metadata file for writing");
        return -1;  // Let the caller handle the failure
    }

    size_t written = fwrite(fileMetaData, sizeof(FileMetadata), 1, fp);
    fclose(fp);

    if (written != 1)
    {
        fprintf(stderr, "ERROR: Failed to write full FileMetadata to %s\n", meta_filepath);
        return -1;
    }

    return 0; // Success
}

/* Read the FileMetadata from a .meta file and print it */
void read_metadata(const char *meta_filename)
{
    FileMetadata fileMetaData;
    FILE *fp = fopen(meta_filename, "rb");
    if (!fp)
    {
        perror("ERROR opening metadata file for reading");
        return;
    }

    if (fread(&fileMetaData, sizeof(FileMetadata), 1, fp) != 1)
    {
        perror("ERROR reading metadata");
        fclose(fp);
        return;
    }
    fclose(fp);

    printf("\n=== Read Metadata ===\n");
    printf("File Name: %s\n", fileMetaData.filename);
    printf("File ID: %zd\n", fileMetaData.fileID);
    printf("Total Chunks: %zd\n", fileMetaData.totalChunk);
    printf("Total Bytes: %zd\n", fileMetaData.totalByte);
    printf("Hash: ");
    for (int i = 0; i < 32; i++)
    {
        printf("%02x", fileMetaData.fileHash[i]);
    }
    printf("\n=====================\n");
}

/*
   Example test function.
   We'll create a metadata file for "gray_cat.png" with fileID=1,
   then write "0001_gray_cat.png.meta" in ./records.
*/
// void test_meta()
// {
//     // We'll assume there's a "records" folder.
//     // If not, you must create it in your environment.
//     FileMetadata *fileMetaData = malloc(sizeof(FileMetadata));
//     if (!fileMetaData)
//     {
//         perror("malloc fileMetaData failed");
//         return;
//     }

//     // Fill out some example data
//     fileMetaData->fileID = 1;
//     strcpy(fileMetaData->filename, "gray_cat.png");

//     // Actually compute the hash, totalByte, totalChunk, etc.
//     create_metadata("gray_cat.png", fileMetaData);

//     // The final meta filename: "0001_gray_cat.png.meta"
//     char meta_filename[512];
//     snprintf(meta_filename, sizeof(meta_filename), "records/%04zd_%s.meta",
//              fileMetaData->fileID, fileMetaData->filename);

//     write_metadata(meta_filename, fileMetaData);

//     // Optional: read it back to confirm
//     read_metadata(meta_filename);

//     free(fileMetaData);
// }

// int main()
// {
//     test_meta();
//     return 0;
// }
