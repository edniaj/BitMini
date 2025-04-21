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
    int byte_read, total_byte_read, total_chunk = 0;
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen(binary_filepath, "rb"); /* Use binary to create the fileHash */

    if (!fp)
    {
        perror("Error opening file");
        return;
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256); // Initialize SHA-256 context

    byte_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp);
    total_byte_read += byte_read;
    while (byte_read > 0)
    {
        SHA256_Update(&sha256, buffer, byte_read); // Update hash with chunk
        byte_read = fread(buffer, sizeof(char), BUFFER_SIZE, fp);
        total_byte_read += byte_read;
    }
    fileMetaData->totalChunk = (total_byte_read + CHUNK_SIZE - 1) / CHUNK_SIZE;
    fileMetaData->totalByte = total_byte_read;
    SHA256_Final(fileMetaData->fileHash, &sha256);

    // Print hash as hex string
    printf("Hashed successfully: ");
    for (int i = 0; i < 32; i++)
    {
        printf("%02x", fileMetaData->fileHash[i]);
    }
    printf("\n");
    fclose(fp);
}

void write_metadata(const char *meta_filepath, const FileMetadata *fileMetaData)
{
    FILE *fp = fopen(meta_filepath, "w");
    if (fp == NULL)
    {
        perror("ERROR opening metadata file for writing");
        exit(EXIT_FAILURE);
    }

    fwrite(fileMetaData, sizeof(FileMetadata), 1, fp);
    fclose(fp);
}

/* Function to read the FileMetadata from a file */
void read_metadata(const char *meta_filename)
{
    FileMetadata fileMetaData;
    FILE *fp = fopen(meta_filename, "rb");
    if (!fp)
    {
        perror("ERROR opening metadata file for reading");
        return;
    }

    /* Read the binary directly into the struct */
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

void test_meta()
{
    char data_filepath[256] = "gray_cat.png";
    char metadata_filepath[256] = "gray_cat.meta";
    FileMetadata *fileMetaData = malloc(sizeof(FileMetadata));
    FileMetadata *testFileMetaData = malloc(sizeof(FileMetadata));

    fileMetaData->fileID = 99;
    strcpy(fileMetaData->filename, "abcde");
    create_metadata(data_filepath, fileMetaData);
    write_metadata(metadata_filepath, fileMetaData);
    read_metadata(metadata_filepath);
    free(fileMetaData);
    free(testFileMetaData);
}
/*
int main(){
    test_meta();
    return 0;
}
*/
