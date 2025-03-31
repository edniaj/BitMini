/************************************************************
 * Forked from seeder.c
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
#include "bitfield.h"

#define STORAGE_DIR "./storage_downloads/"
#define CHUNK_DATA_SIZE 1024

/* ------------------------------------------------------------------------
   Type Definitions (kept in same order)
------------------------------------------------------------------------ */
typedef enum PeerMessageType
{
    MSG_PEER_REQUEST_BITFIELD = 0,
    MSG_SEND_BITFIELD,
    MSG_PEER_REQUEST_CHUNK,
    MSG_SEND_CHUNK
} PeerMessageType;

typedef struct
{
    PeerMessageType type;
    ssize_t bodySize;
} PeerMessageHeader;

typedef struct
{
    ssize_t fileID;
    ssize_t chunkIndex;
} ChunkRequest;

typedef struct
{
    ssize_t fileID;
    ssize_t chunkIndex;
    ssize_t totalByte;
    char chunkData[CHUNK_DATA_SIZE];
    uint8_t chunkHash[32];
} TransferChunk;

typedef struct BitfieldData
{ /* Bitfield response */
    uint8_t *bitfield;
} BitfieldData;

/* The union of all possible payloads */
typedef union
{
    ChunkRequest chunkRequest;
    TransferChunk transferChunk;
    BitfieldData bitfieldData;
    // or a raw buffer if you prefer
    // char raw[8192];
} PeerMessageBody;

/* -------------------------------
   Combine header + body in 1 struct
------------------------------- */
typedef struct
{
    PeerMessageHeader header;
    PeerMessageBody body;
} PeerMessage;

/* ------------------------------------------------------------------------
   (A) MINIMAL TRACKER DEFINITIONS (copied from your tracker side)
   We only include the types needed for:
       - MSG_REQUEST_CREATE_SEEDER
       - MSG_REQUEST_ALL_AVAILABLE_SEED
 ------------------------------------------------------------------------ */
typedef enum TrackerMessageType
{
    MSG_REQUEST_ALL_AVAILABLE_SEED = 0,
    MSG_REQUEST_META_DATA,
    MSG_REQUEST_SEEDER_BY_FILEID,
    MSG_REQUEST_CREATE_SEEDER,
    MSG_REQUEST_DELETE_SEEDER,
    MSG_REQUEST_CREATE_NEW_SEED,
    MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID,
    MSG_REQUEST_UNPARTICIPATE_SEED,
    MSG_ACK_CREATE_NEW_SEED, //  - DONE
    MSG_ACK_PARTICIPATE_SEED_BY_FILEID,
    MSG_ACK_SEEDER_BY_FILEID,
    MSG_RESPOND_ERROR
} TrackerMessageType;

/* We only need PeerInfo + minimal message container */
typedef struct PeerInfo
{
    char ip_address[64];
    char port[16];
} PeerInfo;

typedef struct
{
    char metaFilename[256]; // or whatever size you use
} RequestMetadataBody;

typedef struct TrackerMessageHeader
{
    TrackerMessageType type;
    ssize_t bodySize; // size of body
} TrackerMessageHeader;

typedef struct PeerWithFileID
{
    PeerInfo singleSeeder;
    ssize_t fileID;
} PeerWithFileID;

typedef union
{
    PeerInfo singleSeeder;     // For REGISTER / UNREGISTER
    PeerInfo seederList[64];   // For returning a list of seeders
    FileMetadata fileMetadata; // For CREATE_NEW_SEED
    ssize_t fileID;            // For simple queries
    PeerWithFileID peerWithFileID;
    char raw[512];                       // fallback
    RequestMetadataBody requestMetaData; // ✅ Add this
} TrackerMessageBody;

typedef struct
{
    TrackerMessageHeader header;
    TrackerMessageBody body;
} TrackerMessage;

/*
New Code for Leeching
*/

// seeder.c additions

// Structure to hold seeder information from tracker
typedef struct SeederInfo {
    char ip[16];       // IP address
    int port;          // Port number
    uint8_t *bitfield; // Which chunks the seeder has
    size_t bitfield_size;
    int connected;     // 1 if connection established, 0 otherwise
    int socketfd;      // Socket descriptor if connected
} SeederInfo;

// Structure to hold list of seeders
typedef struct SeederList {
    SeederInfo *seeders;
    int count;
} SeederList;

// Function to get seeder list from tracker
SeederList* get_seeder_list_from_tracker(int tracker_socket, FileMetadata *fileMetaData) {
    // Send request for seeders to tracker
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.type = MSG_REQUEST_SEEDERS; // You'll need to add this type to your enum
    header.fileID = fileMetaData->fileID;
    
    if (write(tracker_socket, &header, sizeof(header)) < 0) {
        perror("ERROR writing seeder request to tracker");
        return NULL;
    }
    
    // Read response header from tracker
    Message response;
    ssize_t nbytes = read(tracker_socket, &response, sizeof(response));
    if (nbytes <= 0) {
        perror("ERROR reading seeder response from tracker");
        return NULL;
    }
    
    if (response.type != MSG_SEND_SEEDERS) { // Add this type to your enum
        fprintf(stderr, "Expected MSG_SEND_SEEDERS, got %d\n", response.type);
        return NULL;
    }
    
    // Read number of seeders
    int seeder_count;
    if (read(tracker_socket, &seeder_count, sizeof(int)) <= 0) {
        perror("ERROR reading seeder count");
        return NULL;
    }
    
    // Allocate seeder list
    SeederList *list = malloc(sizeof(SeederList));
    if (!list) {
        perror("ERROR allocating seeder list");
        return NULL;
    }
    
    list->count = seeder_count;
    list->seeders = malloc(seeder_count * sizeof(SeederInfo));
    if (!list->seeders) {
        perror("ERROR allocating seeder info array");
        free(list);
        return NULL;
    }
    
    // Read seeder information
    for (int i = 0; i < seeder_count; i++) {
        if (read(tracker_socket, &list->seeders[i].ip, sizeof(list->seeders[i].ip)) <= 0 ||
            read(tracker_socket, &list->seeders[i].port, sizeof(list->seeders[i].port)) <= 0) {
            perror("ERROR reading seeder info");
            // Clean up and return NULL
            free(list->seeders);
            free(list);
            return NULL;
        }
        
        // Initialize connection status
        list->seeders[i].connected = 0;
        list->seeders[i].socketfd = -1;
        
        // We'll request bitfields directly from seeders later
        list->seeders[i].bitfield = NULL;
        list->seeders[i].bitfield_size = 0;
    }
    
    printf("Received %d seeders from tracker\n", seeder_count);
    return list;
}

// Function to connect to a seeder
int connect_to_seeder(SeederInfo *seeder) {
    if (seeder->connected) {
        return seeder->socketfd; // Already connected
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return -1;
    }
    
    struct sockaddr_in seeder_addr;
    memset(&seeder_addr, 0, sizeof(seeder_addr));
    seeder_addr.sin_family = AF_INET;
    seeder_addr.sin_addr.s_addr = inet_addr(seeder->ip);
    seeder_addr.sin_port = htons(seeder->port);
    
    if (connect(sockfd, (struct sockaddr *)&seeder_addr, sizeof(seeder_addr)) < 0) {
        perror("ERROR connecting to seeder");
        close(sockfd);
        return -1;
    }
    
    seeder->connected = 1;
    seeder->socketfd = sockfd;
    
    printf("Connected to seeder at %s:%d\n", seeder->ip, seeder->port);
    return sockfd;
}

// Function to request bitfield from a seeder
int request_seeder_bitfield(SeederInfo *seeder, FileMetadata *fileMetaData) {
    int sockfd = seeder->socketfd;
    if (!seeder->connected || sockfd < 0) {
        sockfd = connect_to_seeder(seeder);
        if (sockfd < 0) {
            return -1; // Failed to connect
        }
    }
    
    // Calculate bitfield size
    size_t bitfield_size = calculate_bitfield_size(fileMetaData->totalChunk);
    
    // Send request header
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.type = MSG_REQUEST_BITFIELD;
    header.fileID = fileMetaData->fileID;
    
    if (write(sockfd, &header, sizeof(header)) < 0) {
        perror("ERROR writing bitfield request to seeder");
        close(sockfd);
        seeder->connected = 0;
        seeder->socketfd = -1;
        return -1;
    }
    
    // Read response header
    Message response;
    ssize_t nbytes = read(sockfd, &response, sizeof(response));
    if (nbytes <= 0) {
        perror("ERROR reading bitfield response from seeder");
        close(sockfd);
        seeder->connected = 0;
        seeder->socketfd = -1;
        return -1;
    }
    
    if (response.type != MSG_SEND_BITFIELD) {
        fprintf(stderr, "Expected MSG_SEND_BITFIELD, got %d\n", response.type);
        close(sockfd);
        seeder->connected = 0;
        seeder->socketfd = -1;
        return -1;
    }
    
    // Allocate memory for bitfield if needed
    if (seeder->bitfield == NULL) {
        seeder->bitfield = malloc(bitfield_size);
        if (!seeder->bitfield) {
            perror("ERROR allocating memory for seeder bitfield");
            close(sockfd);
            seeder->connected = 0;
            seeder->socketfd = -1;
            return -1;
        }
        seeder->bitfield_size = bitfield_size;
    }
    
    // Read bitfield data
    ssize_t totalRead = 0;
    while ((size_t)totalRead < bitfield_size) {
        ssize_t r = read(sockfd, seeder->bitfield + totalRead, bitfield_size - totalRead);
        if (r <= 0) {
            perror("ERROR reading bitfield data from seeder");
            close(sockfd);
            seeder->connected = 0;
            seeder->socketfd = -1;
            return -1;
        }
        totalRead += r;
    }
    
    printf("Received bitfield from seeder %s:%d\n", seeder->ip, seeder->port);
    return 0;
}

// Function to request a chunk from a seeder
int request_chunk_from_seeder(SeederInfo *seeder, FileMetadata *fileMetaData, 
                             ssize_t chunkIndex, TransferChunk *outChunk) {
    int sockfd = seeder->socketfd;
    if (!seeder->connected || sockfd < 0) {
        sockfd = connect_to_seeder(seeder);
        if (sockfd < 0) {
            return -1; // Failed to connect
        }
    }
    
    // Check if seeder has this chunk
    if (seeder->bitfield && !bitfield_has_chunk(seeder->bitfield, chunkIndex)) {
        printf("Seeder does not have chunk %zd\n", chunkIndex);
        return -1;
    }
    
    // Send request header
    MessageHeader header;
    memset(&header, 0, sizeof(header));
    header.type = MSG_REQUEST_CHUNK;
    header.fileID = fileMetaData->fileID;
    
    if (write(sockfd, &header, sizeof(header)) < 0) {
        perror("ERROR writing chunk request header to seeder");
        close(sockfd);
        seeder->connected = 0;
        seeder->socketfd = -1;
        return -1;
    }
    
    // Send chunk request
    ChunkRequest chunkReq;
    chunkReq.chunkIndex = chunkIndex;
    
    if (write(sockfd, &chunkReq, sizeof(chunkReq)) < 0) {
        perror("ERROR writing chunk request to seeder");
        close(sockfd);
        seeder->connected = 0;
        seeder->socketfd = -1;
        return -1;
    }
    
    // Read transfer chunk
    ssize_t totalRead = 0;
    char *buf = (char *)outChunk;
    ssize_t chunkSize = sizeof(TransferChunk);
    
    while (totalRead < chunkSize) {
        ssize_t r = read(sockfd, buf + totalRead, chunkSize - totalRead);
        if (r <= 0) {
            perror("ERROR reading TransferChunk from seeder");
            close(sockfd);
            seeder->connected = 0;
            seeder->socketfd = -1;
            return -1;
        }
        totalRead += r;
    }
    
    printf("Received chunk %zd from seeder %s:%d\n", chunkIndex, seeder->ip, seeder->port);
    return 0;
}

// Main leeching function to download a file
int leech_file(SeederList *seeders, FileMetadata *fileMetaData, const char *output_path) {
    if (seeders->count == 0) {
        fprintf(stderr, "No seeders available\n");
        return -1;
    }
    
    // Open output file
    FILE *output_file = fopen(output_path, "wb");
    if (!output_file) {
        perror("ERROR opening output file");
        return -1;
    }
    
    // Create our local bitfield to track which chunks we have
    size_t bitfield_size = calculate_bitfield_size(fileMetaData->totalChunk);
    uint8_t *my_bitfield = calloc(1, bitfield_size);
    if (!my_bitfield) {
        perror("ERROR allocating local bitfield");
        fclose(output_file);
        return -1;
    }
    
    // Request bitfields from all seeders
    for (int i = 0; i < seeders->count; i++) {
        request_seeder_bitfield(&seeders->seeders[i], fileMetaData);
    }
    
    // Download each chunk
    for (ssize_t chunk_idx = 0; chunk_idx < fileMetaData->totalChunk; chunk_idx++) {
        // Skip if we already have this chunk
        if (bitfield_has_chunk(my_bitfield, chunk_idx)) {
            continue;
        }
        
        // Try each seeder until we get the chunk
        TransferChunk chunk;
        int chunk_obtained = 0;
        
        for (int i = 0; i < seeders->count; i++) {
            SeederInfo *seeder = &seeders->seeders[i];
            
            // Skip seeders that don't have this chunk
            if (!seeder->bitfield || !bitfield_has_chunk(seeder->bitfield, chunk_idx)) {
                continue;
            }
            
            // Request chunk from this seeder
            memset(&chunk, 0, sizeof(chunk));
            if (request_chunk_from_seeder(seeder, fileMetaData, chunk_idx, &chunk) == 0) {
                chunk_obtained = 1;
                break;
            }
        }
        
        if (!chunk_obtained) {
            fprintf(stderr, "Failed to download chunk %zd from any seeder\n", chunk_idx);
            continue;
        }
        
        // Write chunk to file
        fseek(output_file, chunk_idx * CHUNK_DATA_SIZE, SEEK_SET);
        fwrite(chunk.chunkData, 1, chunk.totalByte, output_file);
        
        // Update our bitfield
        bitfield_mark_chunk(my_bitfield, chunk_idx);
        
        printf("Downloaded chunk %zd (%zd bytes)\n", chunk_idx, chunk.totalByte);
    }
    
    // Clean up
    fclose(output_file);
    free(my_bitfield);
    
    // Check if we got all chunks
    int complete = 1;
    for (ssize_t i = 0; i < fileMetaData->totalChunk; i++) {
        if (!bitfield_has_chunk(my_bitfield, i)) {
            complete = 0;
            break;
        }
    }
    
    if (complete) {
        printf("File download complete: %s\n", output_path);
        return 0;
    } else {
        printf("File download incomplete: %s\n", output_path);
        return -1;
    }
}

// Use this in your case 4 handler
void handle_leech_file_request(int tracker_socket, FileMetadata *fileMetaData) {
    // Get list of seeders from tracker
    SeederList *seeders = get_seeder_list_from_tracker(tracker_socket, fileMetaData);
    if (!seeders) {
        fprintf(stderr, "Failed to get seeder list from tracker\n");
        return;
    }
    
    // Decide on output filename - you might want to customize this
    char output_path[256];
    snprintf(output_path, sizeof(output_path), "downloaded_file_%ld.dat", fileMetaData->fileID);
    
    // Start leeching process
    leech_file(seeders, fileMetaData, output_path);
    
    // Clean up
    for (int i = 0; i < seeders->count; i++) {
        if (seeders->seeders[i].connected) {
            close(seeders->seeders[i].socketfd);
        }
        if (seeders->seeders[i].bitfield) {
            free(seeders->seeders[i].bitfield);
        }
    }
    free(seeders->seeders);
    free(seeders);
}

/* ------------------------------------------------------------------------
   Function forward declarations - reorganized by category
------------------------------------------------------------------------ */
// Helper/Utility Functions
void request_metadata_by_filename(int tracker_socket, const char *metaFilename, FileMetadata *fileMetaData);

// Tracker Communication Functions
static int connect_to_tracker(const char *tracker_ip, int tracker_port);
static void get_all_available_files(int tracker_socket);
char *get_metadata_via_cli(int tracker_socket, ssize_t *selectedFileID);
static void request_create_new_seed(int tracker_socket, const char *binary_file_path);
static PeerInfo *request_seeder_by_fileID(int tracker_socket, ssize_t fileID, size_t *num_seeders_out);
static void request_participate_seed_by_fileID(int tracker_socket, const char *myIP, const char *myPort, ssize_t fileID);
static void request_create_seeder(int tracker_socket, const char *myIP, const char *myPort);

// Seeder Implementation Functions
int setup_seeder_socket(int port);

// CLI and Control Flow Functions
static void tracker_cli_loop(int tracker_socket, char *ip_address, char *port);

/* ------------------------------------------------------------------------
   Helper/Utility Functions
------------------------------------------------------------------------ */
void request_metadata_by_filename(int tracker_socket, const char *metaFilename, FileMetadata *fileMetaData)
{
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_META_DATA;
    msg.header.bodySize = sizeof(RequestMetadataBody);

    strncpy(msg.body.requestMetaData.metaFilename, metaFilename, sizeof(msg.body.requestMetaData.metaFilename) - 1);

    write(tracker_socket, &msg.header, sizeof(msg.header));
    write(tracker_socket, &msg.body.requestMetaData, msg.header.bodySize);

    // Wait for response
    TrackerMessageHeader respHeader;
    ssize_t n = read(tracker_socket, &respHeader, sizeof(respHeader));
    if (n <= 0)
    {
        perror("Error reading metadata response header");
        return;
    }

    if (respHeader.type != MSG_REQUEST_META_DATA || respHeader.bodySize != sizeof(FileMetadata))
    {
        fprintf(stderr, "Unexpected response from tracker.\n");
        return;
    }

    n = read(tracker_socket, fileMetaData, sizeof(FileMetadata));
    if (n != sizeof(FileMetadata))
    {
        perror("Error reading metadata body");
        return;
    }

    printf("\n📦 Metadata received:\n");
    printf(" • Filename    : %s\n", fileMetaData->filename);
    printf(" • Total Bytes : %zd\n", fileMetaData->totalByte);
    printf(" • Total Chunks: %zd\n", fileMetaData->totalChunk);
    printf(" • File ID     : %zd\n", fileMetaData->fileID);
    printf(" • Hash        : ");
    for (int i = 0; i < 32; i++)
        printf("%02x", fileMetaData->fileHash[i]);
    printf("\n");
}

/* ------------------------------------------------------------------------
   Tracker Communication Functions
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
    msg.header.bodySize = 0; // No body for this request

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

/* Modified to return the metadata filepath */
char *get_metadata_via_cli(int tracker_socket, ssize_t *selectedFileID)
{
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.type = MSG_REQUEST_ALL_AVAILABLE_SEED;
    msg.header.bodySize = 0;

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing header (ALL_AVAILABLE_SEED)");
        return NULL;
    }

    size_t fileCount = 0;
    ssize_t rc = read(tracker_socket, &fileCount, sizeof(fileCount));
    if (rc <= 0)
    {
        char textBuf[256] = {0};
        rc = read(tracker_socket, textBuf, sizeof(textBuf) - 1);
        if (rc > 0)
            printf("Tracker says: %s\n", textBuf);
        else
            perror("ERROR reading from tracker (ALL_AVAILABLE_SEED)");
        return NULL;
    }

    FileEntry *entries = malloc(fileCount * sizeof(FileEntry));
    if (!entries)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    printf("Tracker says there are %zu files.\n", fileCount);

    for (size_t i = 0; i < fileCount; i++)
    {
        rc = read(tracker_socket, &entries[i], sizeof(FileEntry));
        if (rc <= 0)
        {
            perror("ERROR reading file entry from tracker");
            free(entries);
            return NULL;
        }

        printf(" -> FileID: %04zd TotalByte: %zd MetaFile: %s\n",
               entries[i].fileID, entries[i].totalBytes, entries[i].metaFilename);
    }

    // Ask user for fileID
    printf("\nEnter fileID to print its metaFilename:\n");
    char input[256];
    if (!fgets(input, sizeof(input), stdin))
    {
        fprintf(stderr, "Error reading input.\n");
        free(entries);
        return NULL;
    }
    input[strcspn(input, "\n")] = 0;
    *selectedFileID = atoi(input);

    // Search for matching fileID
    const char *metaFilename = NULL;
    for (size_t i = 0; i < fileCount; i++)
    {
        if (entries[i].fileID == *selectedFileID)
        {
            metaFilename = entries[i].metaFilename;
            break;
        }
    }

    if (!metaFilename)
    {
        printf("No file found with ID %zd\n", *selectedFileID);
    }
    else
    {
        printf("📄 metaFilename for fileID %zd: %s\n", *selectedFileID, metaFilename);
    }
    FileMetadata fileMetaData;
    request_metadata_by_filename(tracker_socket, metaFilename, &fileMetaData);

    // Allocate and build path string
    char *metafile_directory = malloc(256);
    if (!metafile_directory)
    {
        perror("malloc failed");
        return NULL;
    }
    snprintf(metafile_directory, 256, "./storage_downloads/%s", metaFilename);

    // Open file for writing
    FILE *metafile_fp = fopen(metafile_directory, "wb");
    if (!metafile_fp)
    {
        perror("Failed to open file for writing");
        free(metafile_directory);
        return NULL;
    }

    // Write metadata to file
    if (fwrite(&fileMetaData, sizeof(FileMetadata), 1, metafile_fp) != 1)
    {
        perror("Failed to write metadata to file");
    }

    fclose(metafile_fp);

    // Save the file ID if requested
    if (selectedFileID)
        *selectedFileID = fileMetaData.fileID;

    // Return the metadata filepath (make sure this is dynamically allocated and caller will free)
    return strdup(metafile_directory);
}

/******************************************************************************
 * 3) request_create_new_seed()
 *    Sends MSG_REQUEST_CREATE_NEW_SEED to the tracker with a new file's metadata.
 ******************************************************************************/
static void request_create_new_seed(int tracker_socket, const char *binary_file_path)
{
    // 1) Build partial metadata (with no final fileID).
    FileMetadata fileMeta;
    memset(&fileMeta, 0, sizeof(fileMeta));
    create_metadata(binary_file_path, &fileMeta);
    fileMeta.fileID = -1; // Let the tracker assign the real fileID

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

    // 7) Generate the .bitfield path and create the bitfield file
    char *bitfieldPath = generate_bitfield_filepath_with_id(newFileID, binary_file_path);
    printf("bitfield path : %s\n", bitfieldPath);
    if (!bitfieldPath)
    {
        fprintf(stderr, "Failed to generate .bitfield path.\n");
    }
    else
    {
        create_filled_bitfield(metaPath, bitfieldPath); // Uses .meta to create .bitfield
    }

    free(metaPath);
}

static PeerInfo *request_seeder_by_fileID(int tracker_socket, ssize_t fileID, size_t *num_seeders_out)
{
    // 1) Build the request
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_SEEDER_BY_FILEID;
    msg.header.bodySize = sizeof(ssize_t);
    msg.body.fileID = fileID;

    // 2) Send header + body
    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing header (REQUEST_SEEDER_BY_FILEID)");
        return NULL;
    }
    if (write(tracker_socket, &msg.body.fileID, msg.header.bodySize) < 0)
    {
        perror("ERROR writing fileID body");
        return NULL;
    }

    // 3) Read the tracker's response header
    TrackerMessageHeader ack_header;
    if (read(tracker_socket, &ack_header, sizeof(ack_header)) <= 0)
    {
        perror("ERROR reading ack header (REQUEST_SEEDER_BY_FILEID)");
        return NULL;
    }

    // 4) Check if it is MSG_ACK_SEEDER_BY_FILEID
    if (ack_header.type != MSG_ACK_SEEDER_BY_FILEID)
    {
        // The tracker might have sent an error message or something else
        fprintf(stderr, "Expected MSG_ACK_SEEDER_BY_FILEID, got %d\n", ack_header.type);
        // You might want to read any text error message at this point
        return NULL;
    }

    // 5) ack_header.bodySize might be 0 if no seeders or `n * sizeof(PeerInfo)`
    if (ack_header.bodySize == 0)
    {
        // Means no seeders found
        printf("No seeders for fileID=%zd.\n", fileID);
        return NULL;
    }

    // 6) Read that many bytes into a PeerInfo array
    size_t num_seeders = ack_header.bodySize / sizeof(PeerInfo);
    PeerInfo *seederList = calloc(num_seeders, sizeof(PeerInfo));
    if (!seederList)
    {
        perror("calloc failed");
        return NULL;
    }

    if (read(tracker_socket, seederList, ack_header.bodySize) < 0)
    {
        perror("ERROR reading seeder list");
        free(seederList);
        return NULL;
    }

    // 7) Print them out (or store them, etc.)
    printf("Received %zu seeders for fileID=%zd:\n", num_seeders, fileID);
    for (size_t i = 0; i < num_seeders; i++)
    {
        printf("  -> %s:%s\n", seederList[i].ip_address, seederList[i].port);
    }

    // Return the number of seeders through the pointer
    if (num_seeders_out)
    {
        *num_seeders_out = num_seeders;
    }

    return seederList;
}

static void request_participate_seed_by_fileID(int tracker_socket,
                                               const char *myIP,
                                               const char *myPort,
                                               ssize_t fileID)
{
    // 1) Build the message
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID;
    msg.header.bodySize = sizeof(PeerWithFileID);

    // Fill out PeerWithFileID
    strncpy(msg.body.peerWithFileID.singleSeeder.ip_address, myIP,
            sizeof(msg.body.peerWithFileID.singleSeeder.ip_address) - 1);
    strncpy(msg.body.peerWithFileID.singleSeeder.port, myPort,
            sizeof(msg.body.peerWithFileID.singleSeeder.port) - 1);
    msg.body.peerWithFileID.fileID = fileID;

    // 2) Send header + body
    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing header (PARTICIPATE_SEED_BY_FILEID)");
        return;
    }
    if (write(tracker_socket, &msg.body.peerWithFileID, msg.header.bodySize) < 0)
    {
        perror("ERROR writing PeerWithFileID body");
        return;
    }

    // 3) Read the tracker's ACK
    TrackerMessageHeader ack_header;
    if (read(tracker_socket, &ack_header, sizeof(ack_header)) <= 0)
    {
        perror("ERROR reading ack header (PARTICIPATE_SEED_BY_FILEID)");
        return;
    }

    // 4) If the tracker uses MSG_ACK_PARTICIPATE_SEED_BY_FILEID on success:
    if (ack_header.type == MSG_ACK_PARTICIPATE_SEED_BY_FILEID)
    {
        printf("Successfully registered as a seeder for fileID %zd.\n", fileID);
        // If ack_header.bodySize > 0, you might read a text message, etc.
        if (ack_header.bodySize > 0)
        {
            char buffer[256];
            ssize_t n = read(tracker_socket, buffer, ack_header.bodySize < 256 ? ack_header.bodySize : 255);
            if (n > 0)
            {
                buffer[n] = '\0';
                printf("Tracker says: %s\n", buffer);
            }
        }
    }
    else
    {
        // Possibly the tracker returned a text-based error
        fprintf(stderr, "Tracker did not ACK participation. Type=%d\n", ack_header.type);

        // read the error text if ack_header.bodySize > 0
        if (ack_header.bodySize > 0)
        {
            char buffer[512];
            ssize_t n = read(tracker_socket, buffer, ack_header.bodySize < 512 ? ack_header.bodySize : 512);
            if (n > 0)
            {
                buffer[n] = '\0';
                fprintf(stderr, "Tracker error: %s\n", buffer);
            }
        }
    }
}

static void request_create_seeder(int tracker_socket, const char *myIP, const char *myPort)
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

    // Read a text response from the tracker (in your code, it's just a string)
    char buffer[256] = {0};
    ssize_t rc = read(tracker_socket, buffer, sizeof(buffer) - 1);
    if (rc > 0)
        printf("Tracker response: %s\n", buffer);
    else
        perror("ERROR reading tracker response (CREATE_SEEDER)");
}

/* ------------------------------------------------------------------------
   Seeder Implementation Functions
------------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------------
   CLI and Control Flow Functions
------------------------------------------------------------------------ */
static void tracker_cli_loop(int tracker_socket, char *ip_address, char *port)
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
        printf("4) Leech file by fileID\n");
        printf("5) Participate seeding by fileID\n");
        printf("0) Exit Tracker\n");
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
            request_create_seeder(tracker_socket, "127.0.0.1", "6000");
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

        case 4:
            /* Leech file by fileID
                1. Get the metadata and store it in ./storage_downloads
                2. Create the bitfield in ./storage_downloads
                3. Get Seeder List
                4. Start leeching the file from the first person. (KISS)
            */

            // 1 Get metadata and store it
            ssize_t *selectedFileID = malloc(sizeof(ssize_t));
            char *metaFilePath = get_metadata_via_cli(tracker_socket, selectedFileID);
            if (!metaFilePath)
            {
                free(selectedFileID);
                break;
            }
            printf("\nmetaFilePath:%s\n", metaFilePath);

            // Process the filepath
            char *filePath = malloc(strlen(metaFilePath) + 1);
            char *lastSlash = strrchr(metaFilePath, '/');
            char *directory = NULL;
            char *baseName = NULL;

            if (!lastSlash)
            {
                directory = ".";
                baseName = metaFilePath;
            }
            else
            {
                // Temporarily null terminate to copy directory
                *lastSlash = '\0';
                directory = strdup(metaFilePath);
                *lastSlash = '/'; // Restore slash
                baseName = lastSlash + 1;
            }

            // Skip the first 5 characters (e.g., "0034_")
            char *fileNameWithoutPrefix = baseName + 5;

            // Copy to filePath excluding .meta extension and prefix
            size_t fileNameLen = strlen(fileNameWithoutPrefix) - 5; // -5 for ".meta"

            // Allocate space for directory + '/' + filename + null terminator
            free(filePath); // Free previous allocation
            filePath = malloc(strlen(directory) + 1 + fileNameLen + 1);

            sprintf(filePath, "%s/%.*s", directory, (int)fileNameLen, fileNameWithoutPrefix);
            printf("filePath:%s\n", filePath);

            if (directory != NULL && directory != ".")
            {
                free(directory);
            }

            // 2 Create bitfield file
            char *bitfieldPath = NULL;
            if (metaFilePath)
            {
                size_t metaPathLen = strlen(metaFilePath);
                bitfieldPath = malloc(metaPathLen + 4 + 1); // +4 is for .bitfield ext and +1 for null terminator

                if (bitfieldPath)
                {
                    strcpy(bitfieldPath, metaFilePath);

                    // Find the .meta extension and replace it with .bitfield
                    char *extension = strstr(bitfieldPath, ".meta");
                    if (extension)
                    {
                        strcpy(extension, ".bitfield");

                        // Create empty bitfield
                        create_empty_bitfield(metaFilePath, bitfieldPath);
                        printf("\nbitfieldPath:%s\n", bitfieldPath);
                    }
                    else
                    {
                        fprintf(stderr, "Error: Metadata file doesn't have expected .meta extension\n");
                    }
                }
            }

            // 3 Get Seeder List
            size_t num_seeders = 0;
            PeerInfo *seederList = request_seeder_by_fileID(tracker_socket, *selectedFileID, &num_seeders);

            // 4 Connect to the first seeder if available
            if (seederList && num_seeders > 0)
            {
                // Make a copy of the first seeder info before freeing the list
                PeerInfo firstSeeder = seederList[0];

                printf("\nAvailable seeders (%zu total):\n", num_seeders);

                // test print to see if the seeder List is correct
                for (size_t i = 0; i < num_seeders; i++)
                {
                    printf("%zu) %s:%s\n", i + 1, seederList[i].ip_address, seederList[i].port);
                }

                printf("\nWill connect to first seeder: %s:%s\n", firstSeeder.ip_address, firstSeeder.port);

                // 4.1 Connect to the first seeder and start leeching
                // (Code for connecting to seeder would go here)

                // Edit this section of the code only GPT 
                        

                // End of edit section

                // Free seederList after using it
                free(seederList);
            }
            else
            {
                printf("No seeders available for this file.\n");
            }

            // Clean up
            if (bitfieldPath)
                free(bitfieldPath);
            free(metaFilePath);
            free(filePath);
            free(selectedFileID);
            break;

        case 5:
            printf("\nEnter fileID:\n");
            if (!fgets(input, 250, stdin))
            {
                printf("Error reading fileID\n");
                break;
            }
            input[strcspn(input, "\n")] = 0; // Trim newline
            ssize_t intput_fileID = (ssize_t)atoi(input);
            request_participate_seed_by_fileID(tracker_socket, ip_address, port, intput_fileID);
            break;

        default:
            printf("Unknown option.\n");
            break;
        }
    }
}

void handle_peer_request(int client_socketfd)
{

    while (1)
    {
        PeerMessageHeader header;
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
        case MSG_PEER_REQUEST_BITFIELD:
        {
            break;
            printf("Peer requests BITFIELD.\n");
            PeerMessageHeader resp_header = {MSG_SEND_BITFIELD, FileMetaData->fileID};
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
        case MSG_PEER_REQUEST_CHUNK:
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
   Main Function - Always Last
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
    tracker_cli_loop(tracker_socket, "127.0.0.1", "6000");
    close(tracker_socket);

    // 4) (Optional) Now also listen for peer connections if you want
    //    them to be able to download chunks from you.
    PeerInfo seeder_info;

    int listen_fd = setup_seeder_socket(6000);
    if (listen_fd < 0)
    {
        // Could not start peer server
        return 1;
    }

    /* 5) Seeding mode
        For the sake of simplicity, all fiels will be held and stored at ./storage_downloads
    */
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
        handle_peer_request(peer_fd);
    }

    // Cleanup if ever reached
    close(listen_fd);

    return 0;
}
