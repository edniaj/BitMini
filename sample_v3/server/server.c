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

#define CHUNK_DATA_SIZE 1024
#define SERVER_PORT 5555

/* Must match how your client defines TransferChunk */
typedef struct TransferChunk
{
    ssize_t fileID;
    ssize_t chunkIndex;
    ssize_t totalByte;
    char chunkData[CHUNK_DATA_SIZE];
    uint8_t chunkHash[32];
} TransferChunk;

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

    if (fread(fileMetaData, sizeof(FileMetadata), 1, meta_file_fp) != 1)
    {
        perror("ERROR reading FileMetadata from gray_cat.meta");
        fclose(meta_file_fp);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }
    fclose(meta_file_fp);

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

    /* Step 4: Open the output file. */
    FILE *receive_file_fp = fopen("received_cat.png", "wb");
    if (!receive_file_fp)
    {
        perror("ERROR opening received_cat.png for writing");
        close(data_socketfd);
        close(listen_socketfd);
        free(fileMetaData);
        exit(EXIT_FAILURE);
    }
    printf("Server expects to receive %zd chunks and %zd total bytes.\n", fileMetaData->totalChunk, fileMetaData->totalByte);
    /* Step 5: Read TransferChunks from the client and write them in the correct order. */
    int chunk_read = 0;
    while (chunk_read < fileMetaData->totalChunk)
    {
        
        TransferChunk chunk;
        ssize_t nbytes = read(data_socketfd, &chunk, sizeof(chunk));
        if (nbytes < 0)
        {
            perror("ERROR reading from socket");
            break;
        }
        if (nbytes == 0)
        {
            // Client closed the connection unexpectedly
            printf("Client closed connection early.\n");
            break;
        }

        
        if (nbytes < (ssize_t)sizeof(chunk))
        {
            fprintf(stderr, "Partial chunk received! Expected %zu, got %zd\n",
                    sizeof(chunk), nbytes);
            break;
        }

        /* Move the fp into the right address before we fwrite it*/
        fseek(receive_file_fp, chunk.chunkIndex * CHUNK_DATA_SIZE, SEEK_SET);
        fwrite(chunk.chunkData, 1, chunk.totalByte, receive_file_fp);

        chunk_read++;
    }


    /* Step 6: Clean up. */
    fclose(receive_file_fp);
    close(data_socketfd);
    close(listen_socketfd);
    free(fileMetaData);

    /* Test code to see if it worked or not */
    if (chunk_read == fileMetaData->totalChunk)
    {
        printf("File transfer complete.\n");
    }
    else
    {
        printf("File transfer incomplete.\n");
    }

    return 0;
}
