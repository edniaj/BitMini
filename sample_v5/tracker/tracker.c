#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#include "database.h"
#include "meta.h"

#define BUFFER_SIZE 1024 * 5
#define SERVER_PORT 5555
#define SERVER_IP "127.0.0.1"

#define MAX_FILES 10000
#define MAX_SEEDERS_PER_FILE 64

/* 🔹 Message Types */
typedef enum
{
    MSG_REQUEST_ALL_AVAILABLE_SEED,
    MSG_REQUEST_META_DATA,
    MSG_REQUEST_SEEDER,
    MSG_REQUEST_CREATE_SEEDER,
    MSG_REQUEST_DELETE_SEEDER,
    MSG_REQUEST_SEEDING_SEED,
    MSG_REQUEST_CREATE_NEW_SEED
} TrackerMessageType;

/* 🔹 Tracker Message Header */
typedef struct TrackerMessageHeader
{
    TrackerMessageType type;
    ssize_t fileID;
} TrackerMessageHeader;

/* 🔹 Peer Info */
typedef struct PeerInfo
{
    char ip_address[64];
    char port[16];
} PeerInfo;

/* 🔹 Message Body */
typedef union TrackerMessageBody
{
    PeerInfo seederInfo;
    FileMetadata fileMetadata;
} TrackerMessageBody;

/* 🔹 Tracker Message */
typedef struct TrackerMessage
{
    TrackerMessageHeader header;
    TrackerMessageBody body;
} TrackerMessage;

/* 🔹 Seeder Tracking */
static PeerInfo g_seeders[MAX_FILES][MAX_SEEDERS_PER_FILE];

/* -------------------------------------------------------------------------- */
/* 🔹 Initialize Seeder List */
void init_seeders()
{
    memset(g_seeders, 0, sizeof(g_seeders));
}

/* -------------------------------------------------------------------------- */
/* 🔹 Setup Tracker Server */
int setup_server()
{
    int listen_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socketfd < 0)
    {
        perror("ERROR opening socket");
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    setsockopt(listen_socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct hostent *server = gethostbyname(SERVER_IP);
    if (server == NULL)
    {
        fprintf(stderr, "ERROR, no such host\n");
        close(listen_socketfd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(SERVER_PORT);

    if (bind(listen_socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR on binding");
        close(listen_socketfd);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_socketfd, 10) < 0)
    {
        perror("ERROR on listen");
        close(listen_socketfd);
        exit(EXIT_FAILURE);
    }

    printf("✅ Tracker listening on %s:%d\n", SERVER_IP, SERVER_PORT);
    return listen_socketfd;
}

/* -------------------------------------------------------------------------- */
/* 🔹 Handle Seeder Registration */
void handle_create_seeder(int client_socket)
{
    TrackerMessage msg;
    if (read(client_socket, &msg, sizeof(msg)) <= 0)
    {
        perror("ERROR reading create seeder request");
        return;
    }

    ssize_t fileID = msg.header.fileID;
    char *ip = msg.body.seederInfo.ip_address;
    char *port = msg.body.seederInfo.port;

    int status = -1;
    for (int i = 0; i < MAX_SEEDERS_PER_FILE; i++)
    {
        if (g_seeders[fileID][i].ip_address[0] == '\0')
        {
            strncpy(g_seeders[fileID][i].ip_address, ip, sizeof(g_seeders[fileID][i].ip_address) - 1);
            strncpy(g_seeders[fileID][i].port, port, sizeof(g_seeders[fileID][i].port) - 1);
            status = 0; // Successfully added
            break;
        }
    }

    char response[256];
    if (status == 0)
        strcpy(response, "Seeder successfully registered.\n");
    else
        strcpy(response, "Failed to register seeder (max limit reached).\n");

    write(client_socket, response, strlen(response));
}

/* -------------------------------------------------------------------------- */
/* 🔹 Handle Request for Available Files */
void handle_request_all_available_files(int client_socket)
{
    size_t fileCount;
    FileEntry *fileList = load_file_entries(&fileCount);

    if (!fileList || fileCount == 0)
    {
        char error_msg[] = "No available files.\n";
        write(client_socket, error_msg, strlen(error_msg));
        return;
    }

    write(client_socket, &fileCount, sizeof(fileCount));

    size_t total_bytes = fileCount * sizeof(FileEntry);
    write(client_socket, fileList, total_bytes);

    free(fileList);
}

/* -------------------------------------------------------------------------- */
/* 🔹 Handle Client Request */
void handle_client(int client_socket)
{
    TrackerMessage msg;
    ssize_t bytes_read = read(client_socket, &msg, sizeof(msg));

    if (bytes_read <= 0)
    {
        perror("ERROR reading from client");
        return;
    }

    printf("Received request type: %d for fileID: %zd\n", msg.header.type, msg.header.fileID);

    switch (msg.header.type)
    {
    case MSG_REQUEST_CREATE_SEEDER:
        handle_create_seeder(client_socket);
        break;

    case MSG_REQUEST_ALL_AVAILABLE_SEED:
        handle_request_all_available_files(client_socket);
        break;

    default:
        {
            char error_msg[] = "Unknown request type.\n";
            write(client_socket, error_msg, strlen(error_msg));
            printf("⚠️  Unknown request type received: %d\n", msg.header.type);
            break;
        }
    }

    printf("Closing connection with client.\n");
    close(client_socket);
}

/* -------------------------------------------------------------------------- */
/* 🔹 Main Tracker Server */
int main()
{
    init_seeders();

    int listen_socketfd = setup_server();

    struct sockaddr_in client_addr;
    socklen_t client_addr_length = sizeof(client_addr);

    while (1)
    {
        int client_socket = accept(listen_socketfd, (struct sockaddr *)&client_addr, &client_addr_length);
        if (client_socket < 0)
        {
            perror("ERROR accepting connection");
            continue;
        }

        printf("✅ New connection established.\n");
        handle_client(client_socket);
    }

    close(listen_socketfd);
    return 0;
}
