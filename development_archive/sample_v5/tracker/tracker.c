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

#define BUFFER_SIZE (1024 * 5)
#define SERVER_PORT 5555
#define SERVER_IP "127.0.0.1"

#define MAX_FILES 10000
#define MAX_SEEDERS_PER_FILE 64
#define MAX_SEEDERS 1000

/* --------------------------------------------------------------------------
   🔹 Message Types
   -------------------------------------------------------------------------- */
typedef enum
{
    MSG_REQUEST_ALL_AVAILABLE_SEED = 0,
    MSG_REQUEST_META_DATA,
    MSG_REQUEST_SEEDER,
    MSG_REQUEST_CREATE_SEEDER,
    MSG_REQUEST_DELETE_SEEDER,
    MSG_REQUEST_SEEDING_SEED,
    MSG_REQUEST_CREATE_NEW_SEED,
    MSG_REQUEST_PARTICIPATE_SEED,
    MSG_REQUEST_UNPARTICIPATE_SEED,
    MSG_ACK_CREATE_NEW_SEED, // ← Add this line
} TrackerMessageType;

/* --------------------------------------------------------------------------
   🔹 Peer Info
   -------------------------------------------------------------------------- */
typedef struct PeerInfo
{
    char ip_address[64];
    char port[16];
} PeerInfo;

/*
   Master array: we can store up to 1000 seeders.
   We'll set any unused slot to have ip_address="", port="".
*/
static PeerInfo list_seeders[MAX_SEEDERS];

/*
   file_to_seeders[fileID][slot] points to one of the PeerInfo in list_seeders
   or NULL if empty.
*/
static PeerInfo *file_to_seeders[MAX_FILES][MAX_SEEDERS_PER_FILE];

/* Keep track of how many seeders we've used in the master array: */

/* --------------------------------------------------------------------------
   🔹 TrackerMessage
   -------------------------------------------------------------------------- */
typedef struct TrackerMessageHeader
{
    TrackerMessageType type;
    ssize_t bodySize; // size of body
} TrackerMessageHeader;

typedef union
{
    PeerInfo singleSeeder;     // For REGISTER / UNREGISTER
    PeerInfo seederList[64];   // For returning a list of seeders
    FileMetadata fileMetadata; // For CREATE_NEW_SEED
    ssize_t fileID;            // For simple queries
    char raw[512];             // fallback
} TrackerMessageBody;

typedef struct
{
    TrackerMessageHeader header;
    TrackerMessageBody body;
} TrackerMessage;

/* --------------------------------------------------------------------------
   (A) init_seeders()
   -------------------------------------------------------------------------- */
void init_seeders(void)
{
    memset(list_seeders, 0, sizeof(list_seeders));
    memset(file_to_seeders, 0, sizeof(file_to_seeders));
}

/* --------------------------------------------------------------------------
   (B) setup_server()
   -------------------------------------------------------------------------- */
int setup_server(void)
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
    if (!server)
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

/* --------------------------------------------------------------------------
   (C) find_peer()
   Returns pointer to PeerInfo in list_seeders if it matches ip+port,
   or NULL if not found.
   -------------------------------------------------------------------------- */
PeerInfo *find_peer(const PeerInfo *p)
{
    for (int i = 0; i < MAX_SEEDERS; i++)
    {
        if (strcmp(list_seeders[i].ip_address, p->ip_address) == 0 &&
            strcmp(list_seeders[i].port, p->port) == 0)
        {
            return &list_seeders[i];
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
   (D) add_peer()
   If we have room, add a new PeerInfo to list_seeders,
   then return a pointer to it. Otherwise return NULL.
   -------------------------------------------------------------------------- */
PeerInfo *add_peer(const PeerInfo *p)
{
    // 1) Look for a free slot in list_seeders
    for (int i = 0; i < MAX_SEEDERS; i++)
    {
        // A "free" slot can be identified by empty ip_address or some "inactive" marker
        if (list_seeders[i].ip_address[0] == '\0')
        {
            // Found an empty slot, fill it
            strcpy(list_seeders[i].ip_address, p->ip_address);
            strcpy(list_seeders[i].port, p->port);
            return &list_seeders[i];
        }
    }

    // If we reach here, no free slot was found
    return NULL;
}

void remove_peer(PeerInfo *p)
{
    // Mark the slot as free by clearing IP/port
    p->ip_address[0] = '\0';
    p->port[0] = '\0';
}

/* --------------------------------------------------------------------------
   (E) add_seeder_to_file()
   Adds a pointer to this PeerInfo in file_to_seeders[fileID].
   For now, we'll assume fileID=0 (or you can pass it in).
   -------------------------------------------------------------------------- */
int add_seeder_to_file(ssize_t fileID, PeerInfo *p)
{
    // First, check if already present
    for (int i = 0; i < MAX_SEEDERS_PER_FILE; i++)
    {
        if (file_to_seeders[fileID][i] == p)
        {
            // It's already in the list for this file
            return 1; // some code meaning "already present"
        }
    }

    // Not found, so find a free slot
    for (int i = 0; i < MAX_SEEDERS_PER_FILE; i++)
    {
        if (file_to_seeders[fileID][i] == NULL)
        {
            file_to_seeders[fileID][i] = p;
            return 0; // success
        }
    }

    // No space available
    return -1;
}

int remove_seeder_from_file(ssize_t fileID, PeerInfo *p)
{
    for (int i = 0; i < MAX_SEEDERS_PER_FILE; i++)
    {
        if (file_to_seeders[fileID][i] == p)
        {
            file_to_seeders[fileID][i] = NULL;
            return 0; // success
        }
    }
    return -1; // not found
}

/* --------------------------------------------------------------------------
   (F) handle_create_seeder()
   "Registers" the seeder into our master array if not present,
   then references it in file_to_seeders[0].
   -------------------------------------------------------------------------- */
void handle_create_seeder(int client_socket, const PeerInfo *p)
{
    // 1) Check if peer already in master array
    PeerInfo *existing = find_peer(p);
    if (existing)
    {
        // It's already known
        printf("Peer already in list: %s:%s\n", p->ip_address, p->port);
        char resp[] = "Seeder already registered in master array.\n";
        write(client_socket, resp, strlen(resp));
        return;
    }

    // 2) If not found, add to master list
    PeerInfo *newPeer = add_peer(p);
    if (!newPeer)
    {
        // Master array is full
        char resp[] = "No space in the master seeder list.\n";
        write(client_socket, resp, strlen(resp));
        return;
    }

    // 3) Respond success to client
    printf("New seeder added to master array: %s:%s\n",
           newPeer->ip_address, newPeer->port);

    char resp[] = "New seeder registered in master array.\n";
    write(client_socket, resp, strlen(resp));
}

/* --------------------------------------------------------------------------
   (G) handle_request_all_available_files()
   Example from your snippet
   -------------------------------------------------------------------------- */
void handle_request_all_available_files(int client_socket)
{
    size_t fileCount = 0;
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

void handle_create_new_seed(int client_socket, const FileMetadata *meta)
{
    // Step 1: Generate a new fileID and write .meta to disk
    ssize_t fileID = add_new_file(meta);
    if (fileID < 0)
    {
        char err[] = "Failed to create new file entry.\n";
        write(client_socket, err, strlen(err));
        return;
    }

    // Step 2: Print info
    printf("\n📦 New Seed Created:\n");
    printf(" • Filename    : %s\n", meta->filename); // original filename from client
    printf(" • Total Chunks: %zd\n", meta->totalChunk);
    printf(" • Total Bytes : %zd\n", meta->totalByte);
    printf(" • fileID      : %zd\n", fileID);
    printf(" • File Hash   : ");
    for (int i = 0; i < 32; i++)
        printf("%02x", meta->fileHash[i]);
    printf("\n");

    // Step 3: Acknowledge with new fileID
    TrackerMessageHeader ack_header;
    ack_header.type = MSG_ACK_CREATE_NEW_SEED;
    ack_header.bodySize = sizeof(ssize_t);

    TrackerMessageBody ack_body = {0};
    ack_body.fileID = fileID;

    write(client_socket, &ack_header, sizeof(ack_header));
    write(client_socket, &ack_body, sizeof(ack_body)); // 👈 send full TrackerMessageBody
}

/* --------------------------------------------------------------------------
   (H) handle_client()
   Reads the header + body from the socket,
   and dispatches to the correct handler.
   -------------------------------------------------------------------------- */
void handle_client(int client_socket)
{
    TrackerMessageHeader header;
    TrackerMessageBody body;
    memset(&header, 0, sizeof(header));
    memset(&body, 0, sizeof(body));
    while (1)
    {
        printf("\nListening to connections :)\n");
        fflush(stdout); // ✅ force print to terminal

        // 1) Read header
        ssize_t bytes_read = read(client_socket, &header, sizeof(header));

        if (bytes_read > 0)
        {
            printf("📩 Read header: type=%d, bodySize=%zd\n", header.type, header.bodySize);
        }
        else

        {
            if (bytes_read < 0)
                perror("ERROR reading header");
            else
                printf("Client disconnected.\n");
            close(client_socket);
            return;
        }

        // Check if bodySize is reasonable
        // 2) If bodySize>0, read body

        // Read the body

        if (header.bodySize > 0)
        {
            bytes_read = read(client_socket, &body, header.bodySize);
            if (bytes_read < 0)
            {
                perror("ERROR reading body");
                close(client_socket);
                return;
            }
            if (bytes_read < header.bodySize)
            {
                fprintf(stderr, "Partial read: expected %zd, got %zd\n", header.bodySize, bytes_read);
                close(client_socket);
                return;
            }
        }

        // Dispatch
        switch (header.type)
        {
        case MSG_REQUEST_CREATE_SEEDER:
        {
            // Expecting a PeerInfo in body.singleSeeder
            if (header.bodySize == sizeof(PeerInfo))
            {
                handle_create_seeder(client_socket, &body.singleSeeder);
            }
            else
            {
                char err[] = "Invalid body size for CREATE_SEEDER.\n";
                write(client_socket, err, strlen(err));
            }
            break;
        }
        case MSG_REQUEST_ALL_AVAILABLE_SEED:
        {
            handle_request_all_available_files(client_socket);
            break;
        }
        case MSG_REQUEST_CREATE_NEW_SEED:
        {
            if (header.bodySize == sizeof(FileMetadata))
            {
                handle_create_new_seed(client_socket, &body.fileMetadata);
            }
            else
            {
                char err[] = "Invalid body size for CREATE_NEW_SEED.\n";
                write(client_socket, err, strlen(err));
            }
            break;
        }

        default:
        {
            char error_msg[] = "Unknown request type.\n";
            write(client_socket, error_msg, strlen(error_msg));
            fprintf(stderr, "⚠️ Unknown request type: %d\n", header.type);
            break;
        }
        }
    } // end while(1)
}

/* --------------------------------------------------------------------------
   (I) Main
   -------------------------------------------------------------------------- */
int main(void)
{
    init_seeders();

    int listen_socketfd = setup_server();

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_length = sizeof(client_addr);

        int client_socket = accept(listen_socketfd,
                                   (struct sockaddr *)&client_addr,
                                   &client_addr_length);
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
