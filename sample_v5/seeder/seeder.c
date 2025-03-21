/************************************************************
 * seeder.c
 ************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>   // for inet_pton(), sockadd_in, etc.
#include <openssl/sha.h> // for create_chunkHash()
#include "meta.h"        // Your FileMetadata struct, etc.
#include "database.h"
/* ------------------------------------------------------------------------
   (A) MINIMAL TRACKER DEFINITIONS (copied from your tracker side)
   We only include the types needed for:
       - MSG_REQUEST_CREATE_SEEDER
       - MSG_REQUEST_ALL_AVAILABLE_SEED
 ------------------------------------------------------------------------ */
typedef enum
{
    MSG_REQUEST_ALL_AVAILABLE_SEED = 0, // ask tracker for full list
    MSG_REQUEST_META_DATA,              // (not used in this sample)
    MSG_REQUEST_SEEDER,
    MSG_REQUEST_CREATE_SEEDER, // register as a seeder
    MSG_REQUEST_DELETE_SEEDER,
    MSG_REQUEST_SEEDING_SEED,
    MSG_REQUEST_CREATE_NEW_SEED
} TrackerMessageType;

/* We only need PeerInfo + minimal message container */
typedef struct PeerInfo
{
    char ip_address[64];
    char port[16];
} PeerInfo;

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

/* ------------------------------------------------------------------------
   (B) FUNCTION: connect_to_tracker()
   Creates a TCP socket and attempts to connect to the given IP/port.
 ------------------------------------------------------------------------ */
static int connect_to_tracker(const char *tracker_ip, int tracker_port)
{
    printf("Connecting to Tracker at %s:%d...\n", tracker_ip, tracker_port);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("ERROR opening socket to tracker");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(tracker_port);

    // Convert IP string to binary form
    if (inet_pton(AF_INET, tracker_ip, &serv_addr.sin_addr) <= 0)
    {
        perror("ERROR invalid tracker IP");
        close(sockfd);
        return -1;
    }

    // Connect to the tracker
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting to tracker");
        printf("❌ Connection to Tracker failed. Is it running at %s:%d?\n", tracker_ip, tracker_port);
        close(sockfd);
        return -1;
    }

    printf("✅ Seeder successfully connected to Tracker at %s:%d\n", tracker_ip, tracker_port);
    return sockfd;
}

/* ------------------------------------------------------------------------
   (C) FUNCTION: register_as_seeder()
   Sends a MSG_REQUEST_CREATE_SEEDER to the tracker, including our IP:port
   so other peers know how to connect to us for chunk downloads.
 ------------------------------------------------------------------------ */
static void register_as_seeder(int tracker_socket, 
                               const char *myIP, const char *myPort)
{
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    // Fill out the message
    msg.header.type = MSG_REQUEST_CREATE_SEEDER;
    msg.header.bodySize = sizeof(PeerInfo);  // <- This is important!
    // (fileID is presumably not used by your tracker in CREATE_SEEDER, 
    // but if you need it, store it in a separate field or union.)

    // Fill in the PeerInfo union
    strncpy(msg.body.singleSeeder.ip_address, myIP, 
            sizeof(msg.body.singleSeeder.ip_address) - 1);
    strncpy(msg.body.singleSeeder.port, myPort, 
            sizeof(msg.body.singleSeeder.port) - 1);

    // Send header+body
    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing tracker header");
        return;
    }
    if (write(tracker_socket, &msg.body, msg.header.bodySize) < 0)
    {
        perror("ERROR writing tracker body");
        return;
    }

    // Or, if you want to send it in one shot, you can do:
    //   write(tracker_socket, &msg, sizeof(msg.header) + msg.header.bodySize)
    // as long as you read them in two steps on the tracker side.

    // Now read the tracker's response
    char buffer[256] = {0};
    ssize_t rc = read(tracker_socket, buffer, sizeof(buffer)-1);
    if (rc > 0)
        printf("Tracker response: %s\n", buffer);
    else
        perror("ERROR reading tracker response");
}

/* ------------------------------------------------------------------------
   (D) FUNCTION: get_all_available_files()
   Sends a MSG_REQUEST_ALL_AVAILABLE_SEED to the tracker to list files.
   In your existing tracker, it first sends a size_t “fileCount” or
   possibly a text if none. Then it sends an array of FileEntry, etc.

   We'll do a minimal read of whatever the tracker responds with.
 ------------------------------------------------------------------------ */
static void get_all_available_files(int tracker_socket)
{
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_ALL_AVAILABLE_SEED;

    // Send request
    if (write(tracker_socket, &msg, sizeof(msg)) < 0)
    {
        perror("ERROR writing request all seeds to tracker");
        return;
    }

    // Try reading first the fileCount (size_t)
    size_t fileCount = 0;
    ssize_t rc = read(tracker_socket, &fileCount, sizeof(fileCount));
    if (rc <= 0)
    {
        // Possibly a text message from tracker. Let's read as text:
        char textBuf[256];
        memset(textBuf, 0, sizeof(textBuf));
        rc = read(tracker_socket, textBuf, sizeof(textBuf) - 1);
        if (rc > 0)
        {
            printf("Tracker says: %s\n", textBuf);
        }
        else
        {
            perror("ERROR reading from tracker");
        }
        return;
    }

    // If we got fileCount, read that many FileEntry structs
    printf("Tracker says there are %zu files.\n", fileCount);
    for (size_t i = 0; i < fileCount; i++)
    {
        FileEntry entry;
        rc = read(tracker_socket, &entry, sizeof(entry));
        if (rc <= 0)
        {
            perror("ERROR reading file entry from tracker");
            return;
        }
        // Print correctly using only fields that exist in FileEntry
        printf(" -> FileID: %04zd TotalByte: %zd MetaFile: %s\n", entry.fileID, entry.totalBytes, entry.metaFilename);
    }
}

/* ------------------------------------------------------------------------
   (E) CLI: Let user pick an action:
       1) Register as a seeder
       2) Get all seeds
       0) Exit
 ------------------------------------------------------------------------ */
static void tracker_cli_loop(int tracker_socket)
{
    while (1)
    {
        printf("\n--- Tracker CLI Options ---\n");
        printf("1) Register as seeder\n");
        printf("2) Get all available files\n");
        printf("0) Exit\n");
        printf("Choose an option: ");

        int choice;
        if (scanf("%d", &choice) != 1)
        {
            printf("Invalid input.\n");
            // Clear out stdin if needed, or break
            break;
        }

        switch (choice)
        {
        case 0:
            printf("Exiting CLI...\n");
            return; // exit the loop

        case 1:
        {
            // The user wants to register as a seeder
            printf("Register as seeder ");


            // In a real scenario, you might detect your IP/port dynamically.

            register_as_seeder(tracker_socket, "127.0.0.1", "6000");
            break;
        }
        case 2:
        {
            // The user wants to list all known seeds
            get_all_available_files(tracker_socket);
            break;
        }
        default:
            printf("Unknown option.\n");
            break;
        }
    }
}

/* ------------------------------------------------------------------------
   (F) Existing "Seeder" chunk-serving code (MINIMALLY CHANGED):
       - initialize_seeder() loads the .meta, .png, etc.
       - setup_seeder_socket() listens for peer connections
       - handle_client_requests() handles chunk requests
 ------------------------------------------------------------------------ */

// These are your original message types for peer <-> seeder
typedef enum
{
    MSG_REQUEST_BITFIELD,
    MSG_SEND_BITFIELD,
    MSG_REQUEST_CHUNK,
    MSG_SEND_CHUNK
} MessageType;

typedef struct MessageHeader
{
    MessageType type;
    ssize_t fileID;
} MessageHeader;

typedef struct ChunkRequest
{
    ssize_t chunkIndex;
} ChunkRequest;

typedef struct TransferChunk
{
    ssize_t fileID;
    ssize_t chunkIndex;
    ssize_t totalByte;
    char chunkData[1024];
    uint8_t chunkHash[32];
} TransferChunk;

static FileMetadata *fileMetaData = NULL;
static FILE *data_file_fp = NULL;
static uint8_t *bitfield = NULL;
static size_t bitfield_size = 0;

static void create_chunkHash(TransferChunk *chunk)
{
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, chunk->chunkData, chunk->totalByte);
    SHA256_Final(chunk->chunkHash, &sha256);
}

int initialize_seeder()
{
    /* Step 1: Load Metadata */
    FILE *meta_file_fp = fopen("gray_cat.meta", "rb");
    if (!meta_file_fp)
    {
        perror("ERROR opening meta file");
        return -1;
    }

    fileMetaData = malloc(sizeof(FileMetadata));
    if (!fileMetaData)
    {
        perror("ERROR allocating FileMetadata");
        fclose(meta_file_fp);
        return -1;
    }

    if (fread(fileMetaData, sizeof(FileMetadata), 1, meta_file_fp) != 1)
    {
        perror("ERROR reading FileMetadata");
        fclose(meta_file_fp);
        free(fileMetaData);
        return -1;
    }
    fclose(meta_file_fp);

    /* Step 2: Open Data File */
    data_file_fp = fopen("gray_cat.png", "rb");
    if (!data_file_fp)
    {
        perror("ERROR opening data file");
        free(fileMetaData);
        return -1;
    }

    /* Step 3: Load Bitfield */
    bitfield_size = (fileMetaData->totalChunk + 7) / 8;
    bitfield = malloc(bitfield_size);
    if (!bitfield)
    {
        perror("ERROR allocating bitfield array");
        fclose(data_file_fp);
        free(fileMetaData);
        return -1;
    }

    FILE *bitfield_file_fp = fopen("gray_cat.bitfield", "rb");
    if (!bitfield_file_fp)
    {
        perror("ERROR opening bitfield file");
        fclose(data_file_fp);
        free(bitfield);
        free(fileMetaData);
        return -1;
    }

    if (fread(bitfield, 1, bitfield_size, bitfield_file_fp) != bitfield_size)
    {
        perror("ERROR reading bitfield data");
        // still proceed
    }
    fclose(bitfield_file_fp);

    printf("Seeder local data initialized successfully.\n");
    return 0;
}

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

void handle_client_requests(int client_socketfd)
{
    while (1)
    {
        MessageHeader header;
        ssize_t nbytes = read(client_socketfd, &header, sizeof(header));
        if (nbytes <= 0)
        {
            if (nbytes < 0)
                perror("ERROR reading from peer socket");
            else
                printf("Peer disconnected.\n");
            break;
        }

        switch (header.type)
        {
        case MSG_REQUEST_BITFIELD:
        {
            printf("Peer requests BITFIELD.\n");
            MessageHeader resp_header = {MSG_SEND_BITFIELD, fileMetaData->fileID};
            if (write(client_socketfd, &resp_header, sizeof(resp_header)) < 0)
            {
                perror("ERROR sending bitfield response header");
                break;
            }
            if (write(client_socketfd, bitfield, bitfield_size) < 0)
            {
                perror("ERROR sending bitfield data");
            }
            else
            {
                printf("Bitfield sent successfully.\n");
            }
            break;
        }
        case MSG_REQUEST_CHUNK:
        {
            ChunkRequest chunkReq;
            if (read(client_socketfd, &chunkReq, sizeof(chunkReq)) <= 0)
            {
                perror("ERROR reading chunk request");
                break;
            }
            printf("Peer wants chunk index: %zd\n", chunkReq.chunkIndex);

            // read chunk data
            TransferChunk chunk;
            memset(&chunk, 0, sizeof(chunk));
            chunk.fileID = fileMetaData->fileID;
            chunk.chunkIndex = chunkReq.chunkIndex;

            // Move file pointer and read
            fseek(data_file_fp, chunkReq.chunkIndex * 1024, SEEK_SET);
            chunk.totalByte = fread(chunk.chunkData, 1, 1024, data_file_fp);

            // Hash
            create_chunkHash(&chunk);

            // Send chunk
            if (write(client_socketfd, &chunk, sizeof(chunk)) < 0)
            {
                perror("ERROR sending chunk");
            }
            else
            {
                printf("Chunk %zd sent successfully.\n", chunkReq.chunkIndex);
            }
            break;
        }
        default:
            printf("Unknown peer message type: %d\n", header.type);
            break;
        }
    }
    close(client_socketfd);
}

/* ------------------------------------------------------------------------
   (G) MAIN: Minimal changes to connect to tracker first, then
             optionally start listening for peer chunk requests.
 ------------------------------------------------------------------------ */
int main()
{
    // 1) Initialize local .meta, .bitfield, etc.
    if (initialize_seeder() != 0)
    {
        fprintf(stderr, "Failed to init seeder.\n");
        return 1;
    }

    // 2) Connect to tracker (change IP/port as needed):
    //    e.g. if your tracker is on 192.168.1.10, port 5555:
    const char *tracker_ip = "127.0.0.1";
    int tracker_port = 5555;
    int tracker_socket = connect_to_tracker(tracker_ip, tracker_port);
    if (tracker_socket < 0)
    {
        printf("socket < 0");
        return 1;
    }

    // 3) Show CLI to user: (Register seeds, list files, etc.)
    tracker_cli_loop(tracker_socket);
    close(tracker_socket);

    // 4) (Optional) Now also listen for peer connections if you want
    //    them to be able to download chunks from you.
    int peer_port = 6000; // or whatever port you want for P2P
    int listen_fd = setup_seeder_socket(peer_port);
    if (listen_fd < 0)
    {
        // Could not start peer server
        return 1;
    }

    // 5) Accept incoming peers in a loop (blocking).
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
        handle_client_requests(peer_fd);
        // closed in handle_client_requests()
    }

    // Cleanup if ever reached
    close(listen_fd);
    fclose(data_file_fp);
    free(bitfield);
    free(fileMetaData);

    return 0;
}
