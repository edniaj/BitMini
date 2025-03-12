// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "meta.h"
#include <unistd.h>
#include <errno.h>

/* ADDED: for SHA-256 (if you need it for create_chunkHash) */
#include <openssl/sha.h>

#define BUFFER_SIZE 1024 * 5
#define CHUNK_DATA_SIZE 1024
#define SERVER_PORT 5555

typedef enum
{
    MSG_REQUEST_BITFIELD,
    MSG_SEND_BITFIELD,
    MSG_REQUEST_CHUNK,
    MSG_SEND_CHUNK
} MessageType;

/* If you want to keep these definitions, that's fine: */
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
  The user-supplied big Message struct. We won't directly read() into this,
  because the union can vary in size. Instead, we'll read the smaller
  “header” first, then do a second read if needed (e.g. for chunkRequest).
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

/* ADDED: A minimal "header" struct for reading from socket first */
typedef struct MessageHeader
{
    MessageType type; // Which message?
    ssize_t fileID;   // e.g. which file
} MessageHeader;

/* ADDED: If you need hashing for the chunk data. */
static void create_chunkHash(TransferChunk *chunk)
{
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, chunk->chunkData, chunk->totalByte);
    SHA256_Final(chunk->chunkHash, &sha256);
}

/* Set up the server socket on port SERVER_PORT. */
int setup_server(struct sockaddr_in *serv_addr)
{
    int listen_socketfd;
    int optval = 1;

    // 1) Create a TCP socket.
    listen_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socketfd < 0)
    {
        perror("ERROR opening socket");
        exit(EXIT_FAILURE);
    }

    // Reuse port while developing.
    setsockopt(listen_socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // 2) Prepare the server address.
    memset(serv_addr, 0, sizeof(*serv_addr));
    serv_addr->sin_family = AF_INET;
    serv_addr->sin_addr.s_addr = INADDR_ANY;
    serv_addr->sin_port = htons(SERVER_PORT);

    // 3) Bind the socket.
    if (bind(listen_socketfd, (struct sockaddr *)serv_addr, sizeof(*serv_addr)) < 0)
    {
        perror("ERROR on binding");
        close(listen_socketfd);
        exit(EXIT_FAILURE);
    }

    return listen_socketfd;
}

/*
UTILS function
*/
int send_chunk(int sockfd, FILE *data_file_fp, struct FileMetaData *fileMetaData, ssize_t chunkIndex)
{
    /* Because we need to send an actual TransferChunk, we first fill it out properly. */
    TransferChunk *chunk = malloc(sizeof(TransferChunk));
    if (!chunk)
    {
        perror("ERROR allocating TransferChunk");
        return 1;
    }
    memset(chunk, 0, sizeof(TransferChunk));

    /* Move the file pointer to the correct chunk index. */
    fseek(data_file_fp, chunkIndex * CHUNK_DATA_SIZE, SEEK_SET);

    chunk->fileID = fileMetaData->fileID;
    chunk->chunkIndex = chunkIndex;
    chunk->totalByte = fread(chunk->chunkData, 1, CHUNK_DATA_SIZE, data_file_fp);

    /* If you want to compute the chunk’s hash: */
    create_chunkHash(chunk);

    /* Now send the TransferChunk over the socket. */
    ssize_t sent_bytes = write(sockfd, chunk, sizeof(TransferChunk));
    if (sent_bytes < 0)
    {
        perror("ERROR writing TransferChunk to socket");
        free(chunk);
        return 1;
    }

    free(chunk);
    return 0;
}

int main()
{
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_addr_length = sizeof(client_addr);

    /* Step 1: Read the meta file that the client presumably created (gray_cat.meta). */
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

    /* Actually read the metadata from the meta file. */
    if (fread(fileMetaData, sizeof(FileMetadata), 1, meta_file_fp) != 1)
    {
        perror("ERROR reading FileMetadata from gray_cat.meta");
        fclose(meta_file_fp);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }
    fclose(meta_file_fp);

    /* Open the actual data file. */
    FILE *data_file_fp = fopen("gray_cat.png", "rb");
    if (!data_file_fp)
    {
        perror("ERROR opening data file");
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }

    /* 
       Open bitfield. 
       Now we use (totalChunk+7)/8 bytes, NOT totalChunk bytes.
    */
    FILE *bitfield_file_fp = fopen("gray_cat.bitfield", "rb");
    if (!bitfield_file_fp)
    {
        perror("ERROR opening bitfield file");
        fclose(data_file_fp);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }

    /* Allocate memory for the bitfield. (1 bit per chunk, so totalChunk bits => (totalChunk+7)/8 bytes) */
    size_t bitfield_size = (fileMetaData->totalChunk + 7) / 8;
    BitfieldData *bitfield_data = malloc(sizeof(BitfieldData));
    if (!bitfield_data)
    {
        perror("ERROR allocating BitfieldData");
        fclose(data_file_fp);
        fclose(bitfield_file_fp);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }

    bitfield_data->bitfield = malloc(bitfield_size);
    if (!bitfield_data->bitfield)
    {
        perror("ERROR allocating bitfield array");
        fclose(data_file_fp);
        fclose(bitfield_file_fp);
        free(bitfield_data);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }

    /* Read the bitfield from the .bitfield file. */
    size_t bytes_read = fread(bitfield_data->bitfield, 1, bitfield_size, bitfield_file_fp);
    if (bytes_read != bitfield_size)
    {
        perror("ERROR reading bitfield data (partial read).");
        /* For brevity, we continue. But you might handle partial reads more carefully. */
    }
    fclose(bitfield_file_fp);

    /* Step 2: Set up server socket and listen. */
    int listen_socketfd = setup_server(&serv_addr);
    if (listen(listen_socketfd, 10) < 0)
    {
        perror("ERROR on listen");
        close(listen_socketfd);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d...\n", SERVER_PORT);

    /* Step 3: Accept a client connection. */
    int data_socketfd = accept(listen_socketfd, (struct sockaddr *)&client_addr, &client_addr_length);
    if (data_socketfd < 0)
    {
        perror("ERROR on accept");
        close(listen_socketfd);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }
    printf("Client connected. Receiving file...\n");

    while (1)
    {
        /*
         * read from socket
         * read will read the whole message but we dont know how big is it.
         * but we know there's a message "header"
         *
         * we only need to handle MSG_REQUEST_BITFIELD and MSG_REQUEST_CHUNK
         * so let's read the header first.
         */
        MessageHeader header;
        ssize_t nbytes = read(data_socketfd, &header, sizeof(header));
        if (nbytes <= 0)
        {
            if (nbytes < 0)
            {
                perror("ERROR reading from socket");
            }
            else
            {
                printf("Client disconnected.\n");
            }
            break; // exit the loop
        }

        /* Based on message header value (header.type), we decide how to read the rest. */
        switch (header.type)
        {
        case MSG_REQUEST_BITFIELD:
        {
            /*
             * The client wants the bitfield. Usually there's no body
             * for this request, so we don't read anything else.
             * Now we "send the bitfield" back.
             */
            printf("Client requests BITFIELD.\n");

            /* We might first send a small "Message" header back. */
            Message response;
            memset(&response, 0, sizeof(response));
            response.type = MSG_SEND_BITFIELD;
            response.fileID = fileMetaData->fileID;

            /* Send the response header. */
            ssize_t sent = write(data_socketfd, &response, sizeof(response));
            if (sent < 0)
            {
                perror("ERROR sending bitfield response header");
                break;
            }

            /* Then send the raw bitfield data. Using bitfield_size bytes. */
            sent = write(data_socketfd, bitfield_data->bitfield, bitfield_size);
            if (sent < 0)
            {
                perror("ERROR sending bitfield data");
            }
            else
            {
                printf("Bitfield (size %zu) sent successfully.\n", bitfield_size);
            }
            break;
        }
        case MSG_REQUEST_CHUNK:
        {
            /*
             * The client is requesting a specific chunk. Now we must read
             * the additional "ChunkRequest" from the socket.
             */
            ChunkRequest chunkReq;
            ssize_t crBytes = read(data_socketfd, &chunkReq, sizeof(chunkReq));
            if (crBytes < 0)
            {
                perror("ERROR reading chunk request from socket");
                break;
            }
            else if (crBytes == 0)
            {
                /* Client closed connection unexpectedly. */
                printf("Client disconnected while sending chunkRequest.\n");
                break;
            }

            printf("Client requests chunk index: %zd\n", chunkReq.chunkIndex);

            /* Respond by sending that chunk. */
            if (send_chunk(data_socketfd, data_file_fp, fileMetaData, chunkReq.chunkIndex) != 0)
            {
                fprintf(stderr, "Failed to send chunk %zd.\n", chunkReq.chunkIndex);
            }
            else
            {
                printf("Chunk %zd sent.\n", chunkReq.chunkIndex);
            }
            break;
        }
        default:
        {
            printf("Unknown message type: %d\n", header.type);
            /* Potentially read and discard any data if the client sent a body. */
            break;
        }
        } // end switch
    }

    /* Step 6: Clean up. */
    close(data_socketfd);
    close(listen_socketfd);
    fclose(data_file_fp);
    free(bitfield_data->bitfield);
    free(bitfield_data);
    free(fileMetaData);

    return 0;
}
