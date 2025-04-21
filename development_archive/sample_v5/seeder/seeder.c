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
    MSG_REQUEST_ALL_AVAILABLE_SEED = 0,
    MSG_REQUEST_META_DATA,
    MSG_REQUEST_SEEDER,
    MSG_REQUEST_CREATE_SEEDER,
    MSG_REQUEST_DELETE_SEEDER,
    MSG_REQUEST_SEEDING_SEED,
    MSG_REQUEST_CREATE_NEW_SEED,
    MSG_REQUEST_PARTICIPATE_SEED,
    MSG_REQUEST_UNPARTICIPATE_SEED,
    MSG_ACK_CREATE_NEW_SEED //  - DONE
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
/******************************************************************************
 * 1) register_as_seeder()
 *    Sends MSG_REQUEST_CREATE_SEEDER to the tracker with our IP:port.
 ******************************************************************************/
static void register_as_seeder(int tracker_socket, const char *myIP, const char *myPort)
{
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    // Fill out header
    msg.header.type = MSG_REQUEST_CREATE_SEEDER;
    msg.header.bodySize = sizeof(PeerInfo);

    // Fill out body (PeerInfo)
    strncpy(msg.body.singleSeeder.ip_address, myIP,
            sizeof(msg.body.singleSeeder.ip_address) - 1);
    strncpy(msg.body.singleSeeder.port, myPort,
            sizeof(msg.body.singleSeeder.port) - 1);

    // 1) Write the header
    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing tracker header (CREATE_SEEDER)");
        return;
    }
    // 2) Write the body (exactly bodySize bytes)
    if (write(tracker_socket, &msg.body.singleSeeder, msg.header.bodySize) < 0)
    {
        perror("ERROR writing tracker body (CREATE_SEEDER)");
        return;
    }

    // Read a text response from the tracker (in your code, it’s just a string)
    char buffer[256] = {0};
    ssize_t rc = read(tracker_socket, buffer, sizeof(buffer) - 1);
    if (rc > 0)
        printf("Tracker response: %s\n", buffer);
    else
        perror("ERROR reading tracker response (CREATE_SEEDER)");
}

/******************************************************************************
 * 2) get_all_available_files()
 *    Sends MSG_REQUEST_ALL_AVAILABLE_SEED to list all files on the tracker.
 ******************************************************************************/
static void get_all_available_files(int tracker_socket)
{
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    // Fill out header
    msg.header.type = MSG_REQUEST_ALL_AVAILABLE_SEED;
    msg.header.bodySize = 0;  // No body for this request

    // 1) Write just the header
    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing header (ALL_AVAILABLE_SEED)");
        return;
    }
    // (No body to write since bodySize=0)

    // --- Read the tracker response ---

    // First try reading a size_t fileCount
    size_t fileCount = 0;
    ssize_t rc = read(tracker_socket, &fileCount, sizeof(fileCount));
    if (rc <= 0)
    {
        // If we fail to read fileCount, maybe tracker sent a text error message
        char textBuf[256];
        memset(textBuf, 0, sizeof(textBuf));
        rc = read(tracker_socket, textBuf, sizeof(textBuf) - 1);
        if (rc > 0)
        {
            printf("Tracker says: %s\n", textBuf);
        }
        else
        {
            perror("ERROR reading from tracker (ALL_AVAILABLE_SEED)");
        }
        return;
    }

    // If we did get fileCount, read that many FileEntry structs
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
        printf(" -> FileID: %04zd TotalByte: %zd MetaFile: %s\n",
               entry.fileID, entry.totalBytes, entry.metaFilename);
    }
}

/******************************************************************************
 * 3) request_create_new_seed()
 *    Sends MSG_REQUEST_CREATE_NEW_SEED to the tracker with a new file’s metadata.
 ******************************************************************************/
static void request_create_new_seed(int tracker_socket, const char *binary_file_path)
{
    // 1) Build partial metadata (with no final fileID).
    FileMetadata fileMeta;
    memset(&fileMeta, 0, sizeof(fileMeta));
    create_metadata(binary_file_path, &fileMeta);
    fileMeta.fileID = -1;  // Let the tracker assign the real fileID

    // 2) Send MSG_REQUEST_CREATE_NEW_SEED to Tracker
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_CREATE_NEW_SEED;
    msg.header.bodySize = sizeof(FileMetadata);
    msg.body.fileMetadata = fileMeta;

    // Send header + body
    write(tracker_socket, &msg.header, sizeof(msg.header));
    write(tracker_socket, &msg.body.fileMetadata, msg.header.bodySize);

    // 3) Read ACK (the new fileID)
    TrackerMessageHeader ack_header;
    read(tracker_socket, &ack_header, sizeof(ack_header));
    if (ack_header.type != MSG_ACK_CREATE_NEW_SEED)
    {
        fprintf(stderr, "Did not receive MSG_ACK_CREATE_NEW_SEED.\n");
        return;
    }

    if (ack_header.bodySize != sizeof(ssize_t))
    {
        fprintf(stderr, "ACK bodySize mismatch.\n");
        return;
    }

    TrackerMessageBody ack_body;
    read(tracker_socket, &ack_body, ack_header.bodySize);

    ssize_t newFileID = ack_body.fileID;
    printf("Tracker assigned fileID: %zd\n", newFileID);

    // 4) Now that we have a fileID, generate .meta path in the same folder as the PNG
    char *metaPath = generate_metafile_filepath_with_id(newFileID, binary_file_path);
    if (!metaPath)
    {
        fprintf(stderr, "Failed to generate .meta path.\n");
        return;
    }

    // 5) Update our FileMetadata with the real fileID
    fileMeta.fileID = newFileID;

    // 6) Write the metadata to disk
    if (write_metadata(metaPath, &fileMeta) != 0) 
    {
        fprintf(stderr, "Failed to write metadata: %s\n", metaPath);
        // handle error if needed
    }
    else
    {
        printf("Wrote metadata file: %s\n", metaPath);
    }

    free(metaPath);
}

/* ------------------------------------------------------------------------
   (E) CLI: Let user pick an action:
       1) Register as a seeder
       2) Get all seeds
       0) Exit
 ------------------------------------------------------------------------ */




static void tracker_cli_loop(int tracker_socket)
{
    char *input = malloc(250);
    if (!input)
    {
        perror("Failed to allocate input buffer");
        return;
    }

    while (1)
    {
        printf("\n--- Tracker CLI Options ---\n");
        printf("1) Register as seeder\n");
        printf("2) Get all available files\n");
        printf("3) Create new seed\n");
        printf("0) Exit\n");
        printf("Choose an option: ");

        if (!fgets(input, 250, stdin))
        {
            printf("Error reading input.\n");
            continue;
        }

        // Remove trailing newline
        input[strcspn(input, "\n")] = 0;

        int choice = atoi(input); // Convert string to integer

        switch (choice)
        {
        case 0:
            printf("Exiting CLI...\n");
            free(input);
            return;

        case 1:
            printf("Registering as seeder...\n");
            register_as_seeder(tracker_socket, "127.0.0.1", "6000");
            break;

        case 2:
            get_all_available_files(tracker_socket);
            break;

        case 3:
            printf("Folder directory: ");
            if (!fgets(input, 250, stdin))
            {
                printf("Error reading directory.\n");
                break;
            }
            input[strcspn(input, "\n")] = 0; // Trim newline
            request_create_new_seed(tracker_socket, input);
            break;

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
    FILE *meta_file_fp = fopen("./storage_metafile/gray_cat.meta", "rb");
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
    data_file_fp = fopen("./storage_downloads/gray_cat.png", "rb");
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

    FILE *bitfield_file_fp = fopen("./storage_bitfield/gray_cat.bitfield", "rb");
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
    // if (initialize_seeder() != 0)
    // {
    //     fprintf(stderr, "Failed to init seeder.\n");
    //     return 1;
    // }

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
