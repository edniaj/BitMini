#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include "peerCommunication.h"

int setup_seeder_socket(int port);
void handle_peer_request(int client_socketfd);
void handle_peer_connection(int listen_fd);
int send_chunk(int sockfd, FILE *data_file_fp, struct FileMetaData *fileMetaData, ssize_t chunkIndex);

int setup_seeder_socket(int port)
{
    int listen_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socketfd < 0)
    {
        perror("ERROR opening seeder socket");
        return -1;
    }
    int optval = 1;
    setsockopt(listen_socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(listen_socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR on binding seeder socket");
        close(listen_socketfd);
        return -1;
    }

    if (listen(listen_socketfd, 10) < 0)
    {
        perror("ERROR on listen");
        close(listen_socketfd);
        return -1;
    }
    printf("Seeder listening on port %d for peer connections...\n", port);
    return listen_socketfd;
}
void handle_peer_connection(int listen_fd)
{
    while (1)
    {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        int peer_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &addr_len);
        if (peer_fd < 0)
        {
            perror("ERROR accepting peer");
            continue;
        }
        printf("New peer connected.\n");
        // Serve chunk requests
        // handle_peer_request(peer_fd);
        // closed in handle_peer_request()
    }

    // Cleanup if ever reached
    close(listen_fd);
}



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

    /* If you want to compute the chunk's hash: */
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



void handle_peer_request(int client_socketfd)
{
    printf("Handling peer request\n");
    while (1)
    {

        // {
        // case MSG_PEER_REQUEST_BITFIELD:

        // {
        //     break;
        // }
        // case MSG_PEER_REQUEST_CHUNK:
        // {
        //     break;
        // }
        // }
    }
}
