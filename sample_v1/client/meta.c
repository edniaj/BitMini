#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct
{
    int fileID;
    char filename[128];
    int totalChunk;
    uint8_t fileHash[32]; // SHA-256 hash
} FileMetadata;

void write_metadata(const char *meta_file, const FileMetadata *meta)
{
    FILE *fp = fopen(meta_file, "wb");
    if (!fp)
    {
        perror("ERROR opening metadata file for writing");
        exit(EXIT_FAILURE);
    }
    if (fwrite(meta, sizeof(FileMetadata), 1, fp) != 1)
    {
        perror("ERROR writing metadata");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    fclose(fp);
}

// Function to read the FileMetadata from a file
void read_metadata(const char *meta_filename, FileMetadata *meta)
{
    FILE *fp = fopen(meta_filename, "rb");
    if (!fp)
    {
        perror("ERROR opening metadata file for reading");
        exit(EXIT_FAILURE);
    }
    if (fread(meta, sizeof(FileMetadata), 1, fp) != 1)
    {
        perror("ERROR reading metadata");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    fclose(fp);
}

int main()
{
    // Example of creating metadata
    FileMetadata meta;
    meta.fileID = 1;
    strncpy(meta.filename, "cat.png", sizeof(meta.filename));

    // Open the file to determine its size
    FILE *fp = fopen(meta.filename, "rb");
    if (!fp)
    {
        perror("ERROR opening file for reading");
        exit(EXIT_FAILURE);
    }
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    fclose(fp);

    // Determine the total number of chunks (for example, chunk size is 1024 bytes)
    int chunk_size = 1024;
    meta.totalChunk = file_size / chunk_size + (file_size % chunk_size != 0);

    // Here you would calculate the SHA-256 hash for the file and store it in meta.fileHash.
    // For demonstration purposes, we'll fill it with dummy data.
    for (int i = 0; i < 32; i++)
    {
        meta.fileHash[i] = i;
    }

    // Write metadata to a file (e.g., "cat.png.metadata")
    write_metadata("cat.png.metadata", &meta);
    printf("Metadata written to cat.png.metadata\n");

    // Read the metadata back from the file
    FileMetadata read_meta;
    read_metadata("cat.png.metadata", &read_meta);

    // Display the read metadata
    printf("File ID: %d\n", read_meta.fileID);
    printf("Filename: %s\n", read_meta.filename);
    printf("Total Chunks: %d\n", read_meta.totalChunk);
    printf("File Hash: ");
    for (int i = 0; i < 32; i++)
    {
        printf("%02x", read_meta.fileHash[i]);
    }
    printf("\n");

    return 0;
}
