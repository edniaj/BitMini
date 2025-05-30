/*
@brief Header files 
peer.h - PLEASE READ THIS FILE, THE STANDARDS FOR HOW THE PROTOCOLS WORK - IS IMPLEMENTED HERE
peerCommunication.h establishes the communication protocol standard between the peer to peer
leech.h establishes the leeching protocol standard between the peer to peer
seed.h establishes the seeding protocol standard between the peer to peer

database.h is not a database
    - MAINLY used in the tracker however there are many utility functions that help us find files in our directory.
    - it acts like a database, managing the files for metadata, binary, bitfield and the metalog which is our "yellow pages"
    - Engineered for perfection

bitfield.h - helped us create and manage the bitfield path
            - however, many of the bit calculation is done inside leech.c 

meta.h - establishes the metadata protocol standard between the peer to peer

arpa/inet.h - for TCP networking. Due to the constrain to man hours, we did not implement the raw socket connection from scratch.
            - we used the arpa/inet.h library to help us with the TCP connection

GENERAL NOTES:
    PEER IS AN FSM, IT HAS THE FEATURES OF A SEEDER AND LEECHER.
    WHEN IT IS CONNECTED TO THE TRACKER, IT WILL HAVE A CLI TO INTERACT WITH THE TRACKER.
    THE TRACKER WILL BE RESPONSIBLE FOR HANDLING THE STATE OF THE NETWORK
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 
#include "meta.h"   
#include "seed.h"
#include "database.h"
#include "bitfield.h"
#include "leech.h"
#include "peer.h"
#include "peerCommunication.h"



#define STORAGE_DIR "./storage_downloads/"
#define CHUNK_DATA_SIZE 1024

#define TRACKER_IP "127.0.0.1"
#define TRACKER_PORT 5555

#define PEER_1_IP "127.0.0.1"
#define PEER_1_PORT "6000"

/**
 * 
 * @brief FSM FOR PEER
 * STATES OF FSM
 * This function handles state transitions and appropriate actions for the peer's FSM.
 * States include:
 * - INIT: Initial state for memory allocation and setup
 * - CONNECTING_TO_TRACKER: Establishing connection to tracker
 * - TRACKER_CONNECTED: Connected to tracker, it will open up a CLI for user to interact with
 * - LISTENING_PEER: Listening for incoming peer connections
 * - SEEDING: Open up socket and actively share file chunks to a connected peer
 * - ERROR: Error handling state
 * - CLEANUP: Resource cleanup state
 * - CLOSING: Final state before termination
 *
 * @return void
 */




PeerContext *peer_ctx;

/* Helper/Utility Functions */
void request_metadata_by_filename(int tracker_socket, const char *metaFilename, FileMetadata *fileMetaData)
{
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_META_DATA;
    msg.header.bodySize = sizeof(RequestMetadataBody);

    strncpy(msg.body.requestMetaData.metaFilename, metaFilename, sizeof(msg.body.requestMetaData.metaFilename) - 1);

    write(tracker_socket, &msg.header, sizeof(msg.header));
    write(tracker_socket, &msg.body.requestMetaData, msg.header.bodySize);

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

char *generate_binary_filepath(char *metaFilePath)
{
    size_t metaPathLen = strlen(metaFilePath);
    char *binaryFilePath = malloc(metaPathLen + 1);

    if (binaryFilePath)
    {
        strcpy(binaryFilePath, metaFilePath);

        char *extension = strstr(binaryFilePath, ".meta");
        if (extension)
        {
            *extension = '\0';
            printf("\nbinaryFilePath: %s\n", binaryFilePath);
            return binaryFilePath;
        }
        else
        {
            fprintf(stderr, "Error: Metadata file doesn't have expected .meta extension\n");
            free(binaryFilePath);
            return NULL;
        }
    }
    return NULL;
}

/* Tracker communication functions*/
int connect_to_tracker()
{

    printf("Connecting to Tracker at %s:%d...\n", TRACKER_IP, TRACKER_PORT);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("ERROR opening socket to tracker");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TRACKER_PORT);

    if (inet_pton(AF_INET, TRACKER_IP, &serv_addr.sin_addr) <= 0)
    {
        perror("ERROR invalid tracker IP");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting to tracker");
        printf("❌ Connection to Tracker failed. Is it running at %s:%d?\n", TRACKER_IP, TRACKER_PORT);
        close(sockfd);
        return -1;
    }

    printf("✅ Seeder successfully connected to Tracker at %s:%d\n", TRACKER_IP, TRACKER_PORT);
    peer_ctx->tracker_fd = sockfd;
    peer_ctx->current_state = Peer_FSM_TRACKER_CONNECTED;
    return sockfd;
}

void disconnect_from_tracker(int tracker_socket)
{
    close(tracker_socket);
    printf("✅ Seeder successfully disconnected from Tracker\n");
}
/**
 * @brief request_create_new_seed
 * Intetion : We want to seed a file that has not be seeded before, thus we need to register the metafile with the tracker
 * This function performs the following operations:
 * 1. Creates metadata for the binary file (hash, size, chunks)
 * 2. Sends the metadata to the tracker for registration
 * 3. Receives a new fileID assigned by the tracker
 *     - tracker will assign a new fileID to the metafile, and then log it into meta.log (this is our yellow pages)
 * 4. Creates a permanent metadata file with the assigned fileID
 * 5. Creates a bitfield file indicating all chunks are available
 *
 *
 * @param tracker_socket Socket descriptor for tracker server connection
 * @param binary_file_path Path to the binary file to be shared
 *
 *
 * This function creates two files on disk:
 *       1. A .meta file containing file metadata
 *       2. A .bitfield file indicating available chunks
 *       Both files will be named based on the fileID assigned by the tracker.
 */
void request_create_new_seed(int tracker_socket, const char *binary_file_path)
{
    // 1) Build partial metadata (with no final fileID).

    FileMetadata fileMeta;
    memset(&fileMeta, 0, sizeof(fileMeta));
    create_metadata(binary_file_path, &fileMeta);
    fileMeta.fileID = -1;

    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_CREATE_NEW_SEED;
    msg.header.bodySize = sizeof(FileMetadata);
    msg.body.fileMetadata = fileMeta;

    ssize_t bytes_written = write(tracker_socket, &msg.header, sizeof(msg.header));
    if (bytes_written != sizeof(msg.header))
    {
        fprintf(stderr, "Failed to send message header\n");
        return;
    }

    bytes_written = write(tracker_socket, &msg.body.fileMetadata, msg.header.bodySize);
    if (bytes_written != msg.header.bodySize)
    {
        fprintf(stderr, "Failed to send message body\n");
        return;
    }

    TrackerMessageHeader ack_header;
    ssize_t bytes_read = read(tracker_socket, &ack_header, sizeof(ack_header));
    if (bytes_read != sizeof(ack_header))
    {
        fprintf(stderr, "Failed to read ACK header\n");
        return;
    }

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

    ssize_t newFileID;
    bytes_read = read(tracker_socket, &newFileID, sizeof(newFileID));
    if (bytes_read != sizeof(newFileID))
    {
        fprintf(stderr, "Failed to read file ID\n");
        return;
    }

    printf("Tracker assigned fileID: %zd\n", newFileID);

    char *metaPath = generate_metafile_filepath_with_id(newFileID, binary_file_path);
    if (!metaPath)
    {
        fprintf(stderr, "Failed to generate .meta path.\n");
        return;
    }

    fileMeta.fileID = newFileID;

    if (write_metadata(metaPath, &fileMeta) != 0)
    {
        fprintf(stderr, "Failed to write metadata: %s\n", metaPath);
        free(metaPath);
        return;
    }

    printf("Wrote metadata file: %s\n", metaPath);

    char *bitfieldPath = generate_bitfield_filepath_with_id(newFileID, binary_file_path);
    if (!bitfieldPath)
    {
        fprintf(stderr, "Failed to generate .bitfield path.\n");
        free(metaPath);
        return;
    }

    printf("bitfield path : %s\n", bitfieldPath);
    create_filled_bitfield(metaPath, bitfieldPath);

    free(metaPath);
    free(bitfieldPath);
}

/**
 * request_seeder_by_fileID
 * @brief Requests a list of peers seeding a specific file from the tracker
 *
 * This function queries the tracker for all peers currently seeding the file
 * identified by fileID. It handles various tracker responses including error cases,
 * blocked file hashes, and blocked IP addresses.
 *
 * @param tracker_socket Socket descriptor for tracker server connection
 * @param fileID The unique identifier of the file to find seeders for
 * @param num_seeders_out We use this to determine the number of seeders, so that we iterate later. it is useful!!
 *
 * @return PeerInfo* Dynamically allocated array of PeerInfo structures containing
 *         information about each seeder (IP address and port). Returns NULL if
 *         no seeders are available or in case of errors.
 *         
 */
PeerInfo *request_seeder_by_fileID(int tracker_socket, ssize_t fileID, size_t *num_seeders_out)
{
    // 1) Build the request
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_SEEDER_BY_FILEID;
    msg.header.bodySize = sizeof(ssize_t);
    msg.body.fileID = fileID;

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

    TrackerMessageHeader ack_header;
    if (read(tracker_socket, &ack_header, sizeof(ack_header)) <= 0)
    {
        perror("ERROR reading ack header (REQUEST_SEEDER_BY_FILEID)");
        return NULL;
    }

    if (ack_header.type == MSG_ACK_FILEHASH_BLOCKED)
    {
        printf("Tracker refused to allow seeding of this file. \nIt is in the blocked list.\n");
        return NULL;
    }
    else if (ack_header.type == MSG_ACK_IP_BLOCKED)
    {
        printf("Your IP is blocked. Tracker refused your ip\n");
        return NULL;
    }

    if (ack_header.type != MSG_ACK_SEEDER_BY_FILEID)
    {
        fprintf(stderr, "Expected MSG_ACK_SEEDER_BY_FILEID, got %d\n", ack_header.type);
        return NULL;
    }

    if (ack_header.bodySize == 0)
    {
        printf("No seeders for fileID=%zd.\n", fileID);
        return NULL;
    }

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

    printf("Received %zu seeders for fileID=%zd:\n", num_seeders, fileID);
    for (size_t i = 0; i < num_seeders; i++)
    {
        printf("  -> %s:%s\n", seederList[i].ip_address, seederList[i].port);
    }

    if (num_seeders_out)
    {
        *num_seeders_out = num_seeders;
    }

    return seederList;
}

/**
 * @brief Registers the current peer as a seeder for a specific file
 *
 * Our parser will allow users to create block list. User might receive msg_ack_ip_blocked or msg_ack_filehash_blocked
 *   - msg_ack_ip_blocked : Your IP is blocked. Tracker refused your ip
 *   - msg_ack_filehash_blocked : Tracker refused to allow seeding of this file. It is in the blocked list.
 *
 * @param tracker_socket Socket descriptor for tracker server connection
 * @param myIP IP address where this peer is listening for connections
 * @param myPort Port number where this peer is listening for connections
 * @param fileID The unique identifier of the file to seed
 *
 * 
 * @note This function requires the peer to have a valid copy of the file
 *       and its metadata before calling.
 */

void request_participate_seed_by_fileID(int tracker_socket, const char *myIP, const char *myPort, ssize_t fileID)
{
    // 1) Build the message
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID;
    msg.header.bodySize = sizeof(PeerWithFileID);

    strncpy(msg.body.peerWithFileID.singleSeeder.ip_address, myIP,
            sizeof(msg.body.peerWithFileID.singleSeeder.ip_address) - 1);
    strncpy(msg.body.peerWithFileID.singleSeeder.port, myPort,
            sizeof(msg.body.peerWithFileID.singleSeeder.port) - 1);
    msg.body.peerWithFileID.fileID = fileID;

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

    TrackerMessageHeader ack_header;
    if (read(tracker_socket, &ack_header, sizeof(ack_header)) <= 0)
    {
        perror("ERROR reading ack header (PARTICIPATE_SEED_BY_FILEID)");
        return;
    }

    if (ack_header.type == MSG_ACK_PARTICIPATE_SEED_BY_FILEID)
    {
        printf("Successfully registered as a seeder for fileID %zd.\n", fileID);
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
    else if (ack_header.type == MSG_ACK_FILEHASH_BLOCKED)
    {
        printf("Tracker refused to allow seeding of this file. \nIt is in the blocked list.\n");
    }
    else if (ack_header.type == MSG_ACK_IP_BLOCKED)
    {
        printf("Your IP is blocked. Tracker refused your ip\n");
    }

    else
    {
        fprintf(stderr, "Tracker did not ACK participation. Type=%d\n", ack_header.type);
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

void request_create_seeder(int tracker_socket, const char *myIP, const char *myPort)
{

    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.type = MSG_REQUEST_CREATE_SEEDER;
    msg.header.bodySize = sizeof(PeerInfo);

    strncpy(msg.body.singleSeeder.ip_address, myIP,
            sizeof(msg.body.singleSeeder.ip_address) - 1);
    strncpy(msg.body.singleSeeder.port, myPort,
            sizeof(msg.body.singleSeeder.port) - 1);

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing tracker header (CREATE_SEEDER)");
        return;
    }
    if (write(tracker_socket, &msg.body.singleSeeder, msg.header.bodySize) < 0)
    {
        perror("ERROR writing tracker body (CREATE_SEEDER)");
        return;
    }

    char buffer[256] = {0};
    ssize_t rc = read(tracker_socket, buffer, sizeof(buffer) - 1);
    if (rc > 0)
        printf("Tracker response: %s\n", buffer);
    else
        perror("ERROR reading tracker response (CREATE_SEEDER)");
}

/**
 * @brief tracker_cli_loop - this is only triggered when the FSM changes to peer_ctx->current_state = Peer_FSM_TRACKER_CLI;
 * 

 * @brief Provides a command-line interface for interacting with the tracker
 *  
 * This function implements the main user interface for the P2P client, offering
 * a menu of operations that can be performed with the tracker and other peers:
 * 
 * - Register as a seeder
 * - List available files in the network
 * - Create a new seed from a local file
 * - Download (leech) a file by its ID
 * - Participate in seeding an existing file
 * - Start seeding mode to serve files to other peers
 *
 * The function handles user input, executes the selected operations, and
 * manages the associated resource allocation and cleanup.
 *
 * @param tracker_socket Socket descriptor for tracker server connection
 * @param ip_address IP address where this peer is accessible
 * @param port Port number where this peer is accessible
 *
 * @return void. Function returns when user selects the exit option or
 *         when certain operations require transition to a different mode.
 * 
 */

void tracker_cli_loop(int tracker_socket, char *ip_address, char *port)
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
        printf("6) Start Seeding\n");
        printf("0) Exit Tracker\n");
        printf("Choose an option: ");

        if (!fgets(input, 250, stdin))
        {
            printf("Error reading input.\n");
            continue;
        }

        input[strcspn(input, "\n")] = 0;

        int choice = atoi(input);

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
            input[strcspn(input, "\n")] = 0;
            request_create_new_seed(tracker_socket, input);
            break;

        case 4:
        {
            ssize_t selectedFileID;
            // User will input the fileID, we will get the metadata filepath
            char *metaFilePath = get_metadata_via_cli(tracker_socket, &selectedFileID);
            char *binary_filepath;
            if (!metaFilePath)
            {
                break;
            }
            printf("\nmetaFilePath:%s\n", metaFilePath);

            char *filePath = malloc(strlen(metaFilePath) + 1);
            char *lastSlash = strrchr(metaFilePath, '/');
            char *directory = NULL;
            char *baseName = NULL;

            if (!lastSlash)
            {
                directory = strdup(".");
                baseName = metaFilePath;
            }
            else
            {
                *lastSlash = '\0';
                directory = strdup(metaFilePath);
                *lastSlash = '/';
                baseName = lastSlash + 1;
            }

            char *fileNameWithoutPrefix = baseName + 5;
            size_t fileNameLen = strlen(fileNameWithoutPrefix) - 5;

            free(filePath);
            filePath = malloc(strlen(directory) + 1 + fileNameLen + 1);
            sprintf(filePath, "%s/%.*s", directory, (int)fileNameLen, fileNameWithoutPrefix);
            printf("filePath:%s\n", filePath);

            free(directory);

            char *bitfieldPath = NULL;
            if (metaFilePath)
            {
                size_t metaPathLen = strlen(metaFilePath);
                bitfieldPath = malloc(metaPathLen + 4 + 1);

                if (bitfieldPath)
                {
                    strcpy(bitfieldPath, metaFilePath);
                    char *extension = strstr(bitfieldPath, ".meta");
                    if (extension)
                    {
                        strcpy(extension, ".bitfield");
                        create_empty_bitfield(metaFilePath, bitfieldPath);
                        printf("\nbitfieldPath:%s\n", bitfieldPath);
                    }
                    else
                    {
                        fprintf(stderr, "Error: Metadata file doesn't have expected .meta extension\n");
                        free(bitfieldPath);
                        bitfieldPath = NULL;
                    }
                }
            }

            binary_filepath = generate_binary_filepath(metaFilePath);
            FileMetadata *fileMetadata = malloc(sizeof(FileMetadata));
            if (!fileMetadata)
            {
                perror("Failed to allocate FileMetadata");
                free(metaFilePath);
                if (bitfieldPath)
                    free(bitfieldPath);
                return;
            }

            read_metadata(metaFilePath, fileMetadata);

            FILE *binary_fp = fopen(binary_filepath, "wb");
            if (!binary_fp)
            {
                perror("Failed to create binary file");
                free(fileMetadata);
                free(metaFilePath);
                if (bitfieldPath)
                    free(bitfieldPath);
                free(binary_filepath);
                return;
            }

            if (fseek(binary_fp, fileMetadata->totalByte - 1, SEEK_SET) != 0)
            {
                perror("Failed to seek in binary file");
                fclose(binary_fp);
                free(fileMetadata);
                free(metaFilePath);
                if (bitfieldPath)
                    free(bitfieldPath);
                free(binary_filepath);
                return;
            }

            if (fwrite("", 1, 1, binary_fp) != 1)
            {
                perror("Failed to write to binary file");
                fclose(binary_fp);
                free(fileMetadata);
                free(metaFilePath);
                if (bitfieldPath)
                    free(bitfieldPath);
                free(binary_filepath);
                return;
            }

            fclose(binary_fp);
            printf("Created empty binary file of size %zd bytes\n", fileMetadata->totalByte);

            size_t num_seeders = 0;
            PeerInfo *seederList = request_seeder_by_fileID(tracker_socket, selectedFileID, &num_seeders);

            if (seederList && num_seeders > 0)
            {
                PeerInfo firstSeeder = seederList[0];

                printf("\nAvailable seeders (%ld total):\n", num_seeders);
                for (size_t i = 0; i < num_seeders; i++)
                {
                    printf("%ld) %s:%s\n", i + 1, seederList[i].ip_address, seederList[i].port);
                }

                printf("\nWill connect to first seeder: %s:%s\n", firstSeeder.ip_address, firstSeeder.port);

                disconnect_from_tracker(tracker_socket);
                int result = leeching(seederList, num_seeders, metaFilePath, bitfieldPath, binary_filepath);
                if (result == 1) {
                    peer_ctx->current_state = Peer_FSM_ERROR;
                    return;
                }
                tracker_socket = connect_to_tracker();
                printf("Reconnected to tracker\n");
                free(seederList);
            }
            else
            {
                printf("No seeders available for this file.\n");
            }

            if (bitfieldPath)
                free(bitfieldPath);
            free(fileMetadata);
            free(metaFilePath);
            free(filePath);
            free(binary_filepath);
            break;
        }

        case 5:
            printf("\nEnter fileID:\n");
            if (!fgets(input, 250, stdin))
            {
                printf("Error reading fileID\n");
                break;
            }
            input[strcspn(input, "\n")] = 0;
            ssize_t input_fileID = (ssize_t)atoi(input);
            request_participate_seed_by_fileID(tracker_socket, ip_address, port, input_fileID);
            break;

        case 6:
            disconnect_from_tracker(tracker_socket);
            int listen_fd = setup_seeder_socket(atoi(PEER_1_PORT));
            if (listen_fd < 0)
            {
                return;
            }

            int return_status = handle_peer_connection(listen_fd);
            if (return_status == 1)
            {
                printf("Error handling peer connection");
                return;
            }

            peer_ctx->current_state = Peer_FSM_LISTENING_PEER;
            return;

            break;
        default:
            printf("Unknown option.\n");
            break;
        }
    }
}

void get_all_available_files(int tracker_socket)
{

    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.type = MSG_REQUEST_ALL_AVAILABLE_SEED;
    msg.header.bodySize = 0;

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing header (ALL_AVAILABLE_SEED)");
        return;
    }

    size_t fileCount = 0;
    ssize_t rc = read(tracker_socket, &fileCount, sizeof(fileCount));
    if (rc <= 0)
    {
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

/**
 * @brief get_metadata_via_cli - User will input the fileID, we will get the metadata filepath
 *
 * This function implements a command-line interface flow to:
 * 1. Request a list of available files from the tracker
 * 2. Display the files to the user
 * 3. Allow the user to select a file by ID from THE list
 * 4. Retrieve the detailed metadata for the selected file
 * 5. Save the metadata to a local file for later use
 *
 * The function uses proper error handling with a cleanup pattern to ensure
 * resources are freed in case of failures.
 *
 * @param tracker_socket Socket descriptor for tracker server connection
 * @param selectedFileID Pointer to store the ID of the file selected by the user
 *
 * @return char* Path to the local metadata file that was created.
 *         Returns NULL if any operation fails.
 */

char *get_metadata_via_cli(int tracker_socket, ssize_t *selectedFileID)
{
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.type = MSG_REQUEST_ALL_AVAILABLE_SEED;
    msg.header.bodySize = 0;

    FileEntry *entries = NULL;
    char *metafile_directory = NULL;
    FILE *metafile_fp = NULL;

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0)
    {
        perror("ERROR writing header (ALL_AVAILABLE_SEED)");
        goto cleanup;
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
        goto cleanup;
    }

    entries = malloc(fileCount * sizeof(FileEntry));
    if (!entries)
    {
        fprintf(stderr, "Memory allocation failed\n");
        goto cleanup;
    }

    printf("Tracker says there are %zu files.\n", fileCount);
    for (size_t i = 0; i < fileCount; i++)
    {
        rc = read(tracker_socket, &entries[i], sizeof(FileEntry));
        if (rc <= 0)
        {
            perror("ERROR reading file entry from tracker");
            goto cleanup;
        }

        printf(" -> FileID: %04zd TotalByte: %zd MetaFile: %s\n",
               entries[i].fileID, entries[i].totalBytes, entries[i].metaFilename);
    }

    printf("\nEnter fileID to print its metaFilename:\n");
    char input[256];
    if (!fgets(input, sizeof(input), stdin))
    {
        fprintf(stderr, "Error reading input.\n");
        goto cleanup;
    }
    input[strcspn(input, "\n")] = 0;
    *selectedFileID = atoi(input);

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
        goto cleanup;
    }

    printf("📄 metaFilename for fileID %zd: %s\n", *selectedFileID, metaFilename);

    FileMetadata fileMetaData;
    request_metadata_by_filename(tracker_socket, metaFilename, &fileMetaData);

    metafile_directory = malloc(256);
    if (!metafile_directory)
    {
        perror("malloc failed");
        goto cleanup;
    }
    snprintf(metafile_directory, 256, "./storage_downloads/%s", metaFilename);

    metafile_fp = fopen(metafile_directory, "wb");
    if (!metafile_fp)
    {
        perror("Failed to open file for writing");
        goto cleanup;
    }

    if (fwrite(&fileMetaData, sizeof(FileMetadata), 1, metafile_fp) != 1)
    {
        perror("Failed to write metadata to file");
        goto cleanup;
    }

    fclose(metafile_fp);
    free(entries);

    *selectedFileID = fileMetaData.fileID;
    return metafile_directory;

cleanup:
    if (entries)
        free(entries);
    if (metafile_fp)
        fclose(metafile_fp);
    if (metafile_directory)
        free(metafile_directory);
    return NULL;
}

/**
 * @brief peer_fsm_handler - Implements the Finite State Machine (FSM) for peer operations
 *
 * This function is the core of the peer's state machine implementation, handling
 * transitions between different operational states:
 *
 * - INIT: Initial state for setting up peer context
 * - CONNECTING_TO_TRACKER: Establishing connection with the tracker server
 * - TRACKER_CONNECTED: Connected to tracker and ready for user interaction
 * - LISTENING_PEER: Listening for incoming peer connections (seeding mode)
 * - SEEDING: Actively sharing files with connected peers
 * - ERROR: Error state for handling unexpected conditions
 * - CLEANUP: Resource cleanup before termination
 * - CLOSING: Final state before program exit
 *
 * Each state has specific entry actions and transition conditions that determine
 * the peer's behavior in the P2P network.
 *
 * @return void
 * 
 * @note The function relies on the global peer_ctx structure to track the
 *       current state and associated resources.
 */

void peer_fsm_handler()
{
    switch (peer_ctx->current_state)
    {
    case Peer_FSM_INIT:
        peer_init();
        peer_ctx->current_state = Peer_FSM_CONNECTING_TO_TRACKER;
        break;

    case Peer_FSM_CONNECTING_TO_TRACKER:
        if (peer_connecting_to_tracker() == 1)
        {
            peer_ctx->current_state = Peer_FSM_ERROR;
        }
        else
        {
            // success
            peer_ctx->current_state = Peer_FSM_TRACKER_CONNECTED;
        }
        break;
    case Peer_FSM_TRACKER_CONNECTED:
        // this function needs to edit
        tracker_cli_loop(peer_ctx->tracker_fd, PEER_1_IP, PEER_1_PORT);
        break;

    case Peer_FSM_LISTENING_PEER:
        if (peer_listening_peer() == 1)
        {
            peer_ctx->current_state = Peer_FSM_ERROR;
        }
        else
        {
            peer_ctx->current_state = Peer_FSM_SEEDING;
        }
        break;

    case Peer_FSM_SEEDING:
        // Handle seeding operations
        if (peer_seeding() == 1)
        {
            peer_ctx->current_state = Peer_FSM_ERROR;
        }
        else
        {
            peer_ctx->current_state = Peer_FSM_LISTENING_PEER;
        }
        break;
    case Peer_FSM_CLEANUP:
        peer_cleanup();
        peer_ctx->current_state = Peer_FSM_CLOSING;
        break;
    case Peer_FSM_ERROR:
        peer_handle_error();
        peer_ctx->current_state = Peer_FSM_CLEANUP;
        break;
    case Peer_FSM_CLOSING:
        peer_closing();
        break;
    default:
        break;
    }
}

void peer_handle_error() {
    printf("Error occurred in peer.c\n");
    return;
}

void peer_init()
{
    peer_ctx = malloc(sizeof(PeerContext));
    if (peer_ctx != NULL)
    {
        memset(peer_ctx, 0, sizeof(PeerContext));
    }
    else
    {
        fprintf(stderr, "Error: Failed to allocate memory for peer_ctx\n");
    }
}

int peer_connecting_to_tracker()
{
    int tracker_fd = connect_to_tracker();
    if (tracker_fd < 0)
    {
        printf("\nConnection to tracker failed. Please restart program\n");
        return 1;
    }
    return 0;
}

int peer_listening_peer()
{
    int listen_fd = setup_seeder_socket(atoi(PEER_1_PORT));
    if (listen_fd < 0)
    {
        printf("Failed to connect to peer");
        return 1;
    }

    peer_ctx->leecher_fd = listen_fd;
    return 0;
}

int peer_seeding()
{

    int return_status = handle_peer_connection(peer_ctx->leecher_fd);

    if (return_status == 1)
    {
        // error
        printf("Error handling peer connection");
        return 1;
    }

    if (return_status == 2)
    {
        // abrupt connection or graceful termination
        printf("Disconnection from peer");
        return 0;
    }

    return 0;
}

void peer_closing()
{
    if (peer_ctx->leecher_fd >= 0)
    {
        close(peer_ctx->leecher_fd);
        peer_ctx->leecher_fd = -1;
    }

    if (peer_ctx->tracker_fd >= 0)
    {
        close(peer_ctx->tracker_fd);
        peer_ctx->tracker_fd = -1;
    }
}

void peer_cleanup() {
    printf("peer_cleanup\n");
    if (!peer_ctx) {
        return;  // Nothing to clean if peer_ctx is NULL
    }
    
    // Close any open file descriptors
    if (peer_ctx->leecher_fd >= 0) {
        close(peer_ctx->leecher_fd);
        peer_ctx->leecher_fd = -1;
    }

    if (peer_ctx->tracker_fd >= 0) {
        close(peer_ctx->tracker_fd);
        peer_ctx->tracker_fd = -1;
    }
    peer_ctx->current_state = Peer_FSM_CLOSING;

}

int main()
{
    // Initialize peer context
    peer_ctx = malloc(sizeof(PeerContext));
    if (!peer_ctx)
    {
        fprintf(stderr, "Failed to allocate memory for peer context\n");
        return 1;
    }

    memset(peer_ctx, 0, sizeof(PeerContext));
    peer_ctx->current_state = Peer_FSM_INIT;

    peer_fsm_handler();

    while (peer_ctx->current_state != Peer_FSM_CLOSING)
    {
        peer_fsm_handler();
    }

    if (peer_ctx->current_state == Peer_FSM_CLOSING)
    {
        peer_closing();
    }

    // Free memory
    free(peer_ctx);
    return 0;
}