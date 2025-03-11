#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <openssl/sha.h> /* SHA-256 hash*/

#include "meta.h"

#define BUFFER_SIZE 1024 * 5 /**/
#define CHUNK_DATA_SIZE 1024 /**/
#define SERVER_PORT 5555
#define SERVER_IP "127.0.0.1"

typedef struct TransferChunk
{
    ssize_t fileID;
    ssize_t chunkIndex;
    ssize_t totalByte;
    char chunkData[CHUNK_DATA_SIZE];
    uint8_t chunkHash[32];
} TransferChunk;

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

int send_chunk(int sockfd, TransferChunk *chunk)
{
    ssize_t sent_bytes = 0;
    /* send chunk */

    /* signed size_t. <0 means error */
    sent_bytes = write(sockfd, chunk, sizeof(TransferChunk));
    if (sent_bytes < 0)
    {
        perror("ERROR writing to socket");
        return 1;
    }

    return 0;
}

void send_file(int sockfd, const char *data_file_path, const char *meta_file_path)
{

    char buffer[BUFFER_SIZE];
    ssize_t chunkIndex = 0;
    TransferChunk *chunk = malloc(sizeof(TransferChunk));
    FILE *data_file_fp = fopen(data_file_path, "rb"); /* read binary */

    struct FileMetaData *fileMetaData = malloc(sizeof(struct FileMetaData)); /* Don't edit this line. */
    FILE *meta_file_fp = fopen(meta_file_path, "rb");                        /* read binary */

    /* Handle malloc errors here */
    if (!chunk)
    {
        perror("Memory allocation failed for chunk");
        return;
    }
    if (!fileMetaData)
    {
        perror("Memory allocation failed for fileMetaData");
        free(chunk);
        return;
    }

    /* Handle error reading files here*/
    if (!data_file_fp)
    {
        perror("ERROR opening data file for reading");
        free(fileMetaData);
        return;
    }
    if (!meta_file_fp)
    {
        perror("ERROR opening metadata file for reading");
        return;
    }

    /* Read the binary directly into the struct of FileMetaData */
    if (fread(fileMetaData, sizeof(FileMetadata), 1, meta_file_fp) != 1)
    {
        perror("ERROR reading metadata");
        fclose(meta_file_fp);
        free(fileMetaData);
        return;
    }
    fclose(meta_file_fp);

    for (chunkIndex = fileMetaData->totalChunk -1; chunkIndex >= 0; chunkIndex--)
    {
        memset(chunk, 0, sizeof(TransferChunk));

        /* Move the file pointer to the correct chunk index BEFORE  we CHUNK IT */
        fseek(data_file_fp, chunkIndex * CHUNK_DATA_SIZE, SEEK_SET);
        chunk->fileID = fileMetaData->fileID;
        chunk->chunkIndex = chunkIndex;
        chunk->totalByte = fread(chunk->chunkData, 1, CHUNK_DATA_SIZE, data_file_fp);
        create_chunkHash(chunk);

        send_chunk(sockfd, chunk);
    }

    printf("File transfer complete.\n");
    free(chunk);
    free(fileMetaData);
    fclose(data_file_fp);
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

    /* Remove newline character if present.
    Send file to the server. */
    printf("Enter the file path: ");
    strcpy(data_file_path, "./gray_cat.png");
    strcpy(meta_file_path, "./gray_cat.meta");
    send_file(sockfd, data_file_path, meta_file_path);
    close(sockfd);

    return EXIT_SUCCESS;
}
