#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h> /* SHA-256 hash*/
#include "meta.h"

#define BUFFER_SIZE 1024 * 5
#define CHUNK_SIZE 1024

/*
metadata should be in json format - readability
filename should be given by User
*/

void create_metadata(char *binary_filepath, FileMetadata *fileMetaData)
{
    int byte_read = 0, total_byte_read = 0;
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen(binary_filepath, "rb"); /* Use binary to create the fileHash */

    if (!fp)
    {
        perror("Error opening file");
        return;
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256); // Initialize SHA-256 context

    while ((byte_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0)
    {
        total_byte_read += byte_read;
        SHA256_Update(&sha256, buffer, byte_read); // Update hash with chunk
    }

    fileMetaData->totalChunk = (total_byte_read + CHUNK_SIZE - 1) / CHUNK_SIZE;
    fileMetaData->totalByte = total_byte_read;
    SHA256_Final(fileMetaData->fileHash, &sha256);

    // Print hash as hex string (just for debug)
    printf("Hashed successfully: ");
    for (int i = 0; i < 32; i++)
    {
        printf("%02x", fileMetaData->fileHash[i]);
    }
    printf("\n");
    fclose(fp);
}

/* Write out the FileMetadata in binary form */
void write_metadata(const char *meta_filepath, const FileMetadata *fileMetaData)
{
    FILE *fp = fopen(meta_filepath, "wb");
    if (fp == NULL)
    {
        perror("ERROR opening metadata file for writing");
        exit(EXIT_FAILURE);
    }
    printf("writing %zd into file of totalbytes", fileMetaData->totalByte);
    fwrite(fileMetaData, sizeof(FileMetadata), 1, fp);
    fclose(fp);
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
void test_meta()
{
    // We'll assume there's a "records" folder.
    // If not, you must create it in your environment.
    FileMetadata *fileMetaData = malloc(sizeof(FileMetadata));
    if (!fileMetaData)
    {
        perror("malloc fileMetaData failed");
        return;
    }

    // Fill out some example data
    fileMetaData->fileID = 1;
    strcpy(fileMetaData->filename, "gray_cat.png");

    // Actually compute the hash, totalByte, totalChunk, etc.
    create_metadata("gray_cat.png", fileMetaData);

    // The final meta filename: "0001_gray_cat.png.meta"
    char meta_filename[512];
    snprintf(meta_filename, sizeof(meta_filename), "records/%04zd_%s.meta",
             fileMetaData->fileID, fileMetaData->filename);

    write_metadata(meta_filename, fileMetaData);

    // Optional: read it back to confirm
    read_metadata(meta_filename);

    free(fileMetaData);
}

int main()
{
    test_meta();
    return 0;
}
