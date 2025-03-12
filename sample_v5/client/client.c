#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/sha.h> /* SHA-256 hash*/

#include "meta.h"

#define BUFFER_SIZE 1024 * 5 /**/
#define CHUNK_DATA_SIZE 1024 /**/
#define SERVER_PORT 5555
#define SERVER_IP "127.0.0.1"

#define MAX_BITFIELD_SIZE 1024 * 1024 * 10 /* 10mb */

/*
This guy is the leecher since he is attempting to connect to the server
*/
typedef enum
{
    MSG_REQUEST_BITFIELD,
    MSG_SEND_BITFIELD,
    MSG_REQUEST_CHUNK,
    MSG_SEND_CHUNK
} MessageType;

typedef struct BitfieldData
{ /* Bitfield response */
    uint8_t *bitfield;
} BitfieldData;

typedef struct ChunkRequest
{
    ssize_t chunkIndex;
} ChunkRequest;

typedef struct TransferChunk
{
    ssize_t fileID;
    ssize_t chunkIndex;
    ssize_t totalByte;
    char chunkData[CHUNK_DATA_SIZE];
    uint8_t chunkHash[32];
} TransferChunk;

/*
  We won't directly read() into this big union from the server,
  because we might first read a smaller "header". Then, depending
  on the message type, we read the relevant portion (e.g. a chunk).
*/
typedef union MessageBody
{
    ChunkRequest chunkRequest;
    BitfieldData bitfieldData;
    TransferChunk transferChunk;
} MessageBody;

typedef struct Message
{
    MessageType type; // Determines what data is relevant
    ssize_t fileID;   // Request for BITFIELD or CHUNK
    MessageBody messageBody;
} Message;

/* Minimal "header" struct for a header-first approach */
typedef struct MessageHeader
{
    MessageType type; // Which message?
    ssize_t fileID;   // e.g. which file
} MessageHeader;

/* --------------------------------------------------------------------- */

int setup_connection()
{
    /*
    socketfd : TCP port
    AF_INET -> IP v4
    AF_INET6 -> IP v6
    SOCK_STREAM -> Helps us with the TCP handshake
    SOCK_RAW -> We implement the TCP handshake ourself, rather than SOCK_STREAM that abstracts the TCP handshake away
    */
    int client_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *server = gethostbyname(SERVER_IP);
    struct sockaddr_in serv_addr;

    if (client_socketfd < 0)
    {
        perror("ERROR opening socket");
        return -1;
    }

    if (server == NULL)
    {
        fprintf(stderr, "ERROR, no such host\n");
        close(client_socketfd);
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(SERVER_PORT); /* Server's port */

    // Connect to the server.
    if (connect(client_socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting");
        close(client_socketfd);
        return -1;
    }

    printf("Connected to server.\n");
    return client_socketfd;
}

void create_chunkHash(TransferChunk *chunk)
{
    SHA256_CTX sha256;
    SHA256_Init(&sha256); // Initialize SHA-256 context

    SHA256_Update(&sha256, chunk->chunkData, chunk->totalByte); // Update hash with chunk
    SHA256_Final(chunk->chunkHash, &sha256);
}

/*
   Request the bitfield from the server, but note that
   we expect `(totalChunk+7)/8` bytes, not `totalChunk` bytes.
*/
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

/*
   Example of how you might request a single chunk from the server:
   1) Send a small header with type = MSG_REQUEST_CHUNK, plus the fileID.
   2) Send an additional 'ChunkRequest' struct containing the chunkIndex.
   3) Read back a 'TransferChunk' from the server into outChunk.
*/
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

/*
   We won't really use send_chunk() from the client side in this scenario,
   because the client is only "receiving" chunks from the server.
   (Kept here as in your original code.)
*/
int send_chunk(int sockfd, TransferChunk *chunk)
{
    ssize_t sent_bytes = write(sockfd, chunk, sizeof(TransferChunk));
    if (sent_bytes < 0)
    {
        perror("ERROR writing to socket");
        return 1;
    }
    return 0;
}

/* Helper function to test if bit i in bitfield is set (1) */
static int bitfield_has_chunk(const uint8_t *bitfield, ssize_t i)
{
    ssize_t byteIndex = i / 8;
    int bitOffset = i % 8;
    /* 
       If the bit is set, it returns nonzero.
       We'll specifically check if that bit is 1. 
    */
    return (bitfield[byteIndex] & (1 << bitOffset)) != 0;
}

/* Helper function to set bit i in our local bitfield. */
static void bitfield_mark_chunk(uint8_t *bitfield, ssize_t i)
{
    ssize_t byteIndex = i / 8;
    int bitOffset = i % 8;
    bitfield[byteIndex] |= (1 << bitOffset);
}

int main()
{
    char data_file_path[256];
    char meta_file_path[256];
    /* Clean out the garbage values*/
    memset(data_file_path, 0, 256);
    memset(meta_file_path, 0, 256);

    int sockfd = setup_connection();
    if (sockfd == -1)
    {
        fprintf(stderr, "Failed to establish connection.\n");
        return EXIT_FAILURE;
    }

    /* Prepare to leech file */
    FILE *receive_file_fp = fopen("received_cat.png", "wb");
    if (!receive_file_fp)
    {
        perror("ERROR opening received_cat.png for writing");
        exit(EXIT_FAILURE);
    }

    /*
       Let's read "gray_cat.meta" so we know how many chunks exist,
       the fileID, etc. (You had asked to do that in your code).
    */
    FILE *meta_file_fp = fopen("gray_cat.meta", "rb");
    if (!meta_file_fp)
    {
        perror("ERROR opening meta file");
        exit(EXIT_FAILURE);
    }

    FileMetadata *fileMetaData = malloc(sizeof(FileMetadata));
    if (!fileMetaData)
    {
        perror("ERROR allocating FileMetadata");
        fclose(meta_file_fp);
        exit(EXIT_FAILURE);
    }

    if (fread(fileMetaData, sizeof(FileMetadata), 1, meta_file_fp) != 1)
    {
        perror("ERROR reading FileMetadata from gray_cat.meta");
        fclose(meta_file_fp);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }
    fclose(meta_file_fp);

    /*
       Let's start with requesting the seeder's bitfield.
       We'll compute how many bytes that bitfield should be:
    */
    uint8_t *seederBitfield = request_bitfield(sockfd, fileMetaData->fileID, fileMetaData->totalChunk);
    if (!seederBitfield)
    {
        fprintf(stderr, "Failed to retrieve bitfield from server.\n");
        goto cleanup;
    }

    /* 
       Make our local bitfield (also using (totalChunk+7)/8 bytes).
       Each bit = "do we have chunk i?" 
       Start with all bits = 0 (we have none).
    */
    size_t bitfield_size = (fileMetaData->totalChunk + 7) / 8;
    uint8_t *myBitfield = calloc(bitfield_size, 1);
    if (!myBitfield)
    {
        perror("ERROR allocating local bitfield");
        goto cleanup;
    }

    /*
       Loop across all chunk indices from 0..(totalChunk-1).
       Then interpret the bits in seederBitfield to see if the seeder has chunk i.
       If seeder has it and we do not, we request that chunk from the server.
       On success, mark that bit in our local bitfield as 1.
    */
    for (ssize_t i = 0; i < fileMetaData->totalChunk; i++)
    {
        if (bitfield_has_chunk(seederBitfield, i) && !bitfield_has_chunk(myBitfield, i))
        {
            /* We do not have chunk i, but seeder does, so request it. */
            TransferChunk chunk;
            memset(&chunk, 0, sizeof(chunk));

            if (request_chunk(sockfd, fileMetaData, i, &chunk) != 0)
            {
                fprintf(stderr, "Failed to request chunk %zd.\n", i);
                continue; // skip
            }

            /* We got the chunk, so let's write it into our file at the correct offset. */
            fseek(receive_file_fp, chunk.chunkIndex * CHUNK_DATA_SIZE, SEEK_SET);
            fwrite(chunk.chunkData, 1, chunk.totalByte, receive_file_fp);

            /* Mark our local bitfield that we now have chunk i */
            bitfield_mark_chunk(myBitfield, i);

            printf("Retrieved chunk %zd, wrote %zd bytes.\n", i, chunk.totalByte);
        }
    }

    /*
       Old Code from previous client model:
       Remove newline character if present.
       Send file to the server.
       printf("Enter the file path: ");
       strcpy(data_file_path, "./gray_cat.png");
       strcpy(meta_file_path, "./gray_cat.meta");
       send_file(sockfd, data_file_path, meta_file_path);
       close(sockfd);
    */

    printf("File download completed.\n");

cleanup:
    fclose(receive_file_fp);

    if (fileMetaData)
        free(fileMetaData);
    if (seederBitfield)
        free(seederBitfield);
    if (myBitfield)
        free(myBitfield);

    /* Finally, close the socket and exit. */
    close(sockfd);
    return EXIT_SUCCESS;
}
