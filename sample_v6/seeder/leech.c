#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include "peerCommunication.h"
#include "leech.h"
#include "meta.h"
#define STORAGE_DIR "./storage_downloads/"

int request_chunk(int sockfd, FileMetadata *fileMetaData, ssize_t chunkIndex, TransferChunk *outChunk);
uint8_t *request_bitfield(int sockfd, ssize_t fileID, ssize_t totalChunk);

uint8_t *request_bitfield(int sockfd, ssize_t fileID, ssize_t totalChunk)
{
    // Calculate how many bytes we expect for the bitfield
    size_t bitfield_size = (totalChunk + 7) / 8;

    // 1) Send the header for "request bitfield"
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.type = MSG_REQUEST_BITFIELD;
    header.fileID = fileID;

    if (write(sockfd, &header, sizeof(header)) < 0)
    {
        perror("ERROR writing bitfield request header to server");
        return NULL;
    }

    // 2) Read the server's response (which includes type, fileID, etc.)
    Message response;
    ssize_t nbytes = read(sockfd, &response, sizeof(response));
    if (nbytes <= 0)
    {
        perror("ERROR reading bitfield response header from server");
        return NULL;
    }

    if (response.type != MSG_SEND_BITFIELD)
    {
        fprintf(stderr, "Expected MSG_SEND_BITFIELD, got %d\n", response.type);
        return NULL;
    }

    // 3) Now read the actual bitfield data from server:
    //    We expect bitfield_size bytes total.
    uint8_t *bitfield = malloc(bitfield_size);
    if (!bitfield)
    {
        perror("ERROR allocating memory for bitfield");
        return NULL;
    }

    ssize_t totalRead = 0;
    while ((size_t)totalRead < bitfield_size)
    {
        ssize_t r = read(sockfd, bitfield + totalRead, bitfield_size - totalRead);
        if (r <= 0)
        {
            perror("ERROR reading bitfield data");
            free(bitfield);
            return NULL;
        }
        totalRead += r;
    }

    printf("Received bitfield of %zu bytes.\n", bitfield_size);
    return bitfield;
}
int request_chunk(int sockfd, FileMetadata *fileMetaData, ssize_t chunkIndex, TransferChunk *outChunk)
{
    // 1) Create and send the header.
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.type = MSG_REQUEST_CHUNK;
    header.fileID = fileMetaData->fileID;

    if (write(sockfd, &header, sizeof(header)) < 0)
    {
        perror("ERROR writing chunk request header to server");
        return -1;
    }

    // 2) Send the 'ChunkRequest' struct
    ChunkRequest chunkReq;
    chunkReq.chunkIndex = chunkIndex;

    if (write(sockfd, &chunkReq, sizeof(chunkReq)) < 0)
    {
        perror("ERROR writing chunk request body to server");
        return -1;
    }

    // 3) Read back the TransferChunk
    ssize_t totalRead = 0;
    char *buf = (char *)outChunk;
    ssize_t chunkSize = sizeof(TransferChunk);

    while (totalRead < chunkSize)
    {
        ssize_t r = read(sockfd, buf + totalRead, chunkSize - totalRead);
        if (r <= 0)
        {
            perror("ERROR reading TransferChunk from server");
            return -1;
        }
        totalRead += r;
    }

    // (Optional) Validate the chunk hash or do any checks here.

    return 0;
}


void send_peer_request(int server_socketfd)
{

    // switch (unsure)
    // {
    // case MSG_PEER_REQUEST_BITFIELD:
    // {
    //     printf("Requesting BITFIELD.\n");
    //     break;
    // }

    // case MSG_PEER_REQUEST_CHUNK:
    // {
    //     printf("Requesting CHUNK.\n");
    //     break;
    // }
    // }
    return;
}




int leeching(PeerInfo *seeder_list, size_t num_seeders, char *metadata_filepath, char *bitfield_filepath, char *binary_filepath)
{
    int index = 0;
    FileMetadata *fileMetaData = malloc(sizeof(FileMetadata));

    read_metadata(metadata_filepath, fileMetaData);

    // Read bitfield from the bitfield_filepath
    FILE *bitfield_fp = fopen(bitfield_filepath, "rb");
    if (!bitfield_fp)
    {
        perror("ERROR opening bitfield file");
        return 1;
    }

    size_t bitfield_size = (fileMetaData->totalChunk + 7) / 8; // Round up division
    uint8_t *bitfield = malloc(bitfield_size);                 // each uint8_t is 1 byte which represents 8 chunks.
    fread(bitfield, 1, bitfield_size, bitfield_fp);

    // Request Bitfield from the seeder
    


    // We should write a binary file to the disk based on the total bitsize in the metadata file.
    FILE * metadata_fp = fopen(metadata_filepath, "rb");
    if (!metadata_fp)
    {
        perror("ERROR opening metadata file");
        return 1;
    }
    


    for (index = 0; index < num_seeders; index++)
    {
        // leech_from_seeder(seeder_list[index], binary_filepath, bitf 0ield_filepath);
        break;
    }
    
    // We will see if we completed all bits in the bitfield.

    // We will run hash check on the binary file and see if it matches

    printf("\n");
    free(fileMetaData);
    free(bitfield);
    return 0;
}