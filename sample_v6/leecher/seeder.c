/************************************************************
 * Forked from seeder.c
 ************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> // for inet_pton(), sockadd_in, etc.

#include "meta.h" // Your FileMetadata struct, etc.
#include "database.h"
#include "bitfield.h"
#include "leech.h"
#include "seed.h"

#define STORAGE_DIR "./storage_downloads/"
#define CHUNK_DATA_SIZE 1024

#define TRACKER_IP "127.0.0.1"
#define TRACKER_PORT 5555

#define PEER_1_IP "127.0.0.1"
#define PEER_1_PORT "6000"
/* ------------------------------------------------------------------------
   Type Definitions (kept in same order)
------------------------------------------------------------------------ *
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

/* Function declaration*/
/* Seeder -> Tracker FUNCTIONS*/
char *get_metadata_via_cli(int tracker_socket, ssize_t *selectedFileID);
static void get_all_available_files(int tracker_socket);
static void tracker_cli_loop(int tracker_socket, char *ip_address, char *port);
static void request_create_seeder(int tracker_socket, const char *myIP, const char *myPort);
static void request_participate_seed_by_fileID(int tracker_socket, const char *myIP, const char *myPort, ssize_t fileID);
static PeerInfo *request_seeder_by_fileID(int tracker_socket, ssize_t fileID, size_t *num_seeders_out);
static void request_create_new_seed(int tracker_socket, const char *binary_file_path);
void request_metadata_by_filename(int tracker_socket, const char *metaFilename, FileMetadata *fileMetaData);
static int connect_to_tracker();
static void disconnect_from_tracker(int tracker_socket);
char *generate_binary_filepath(char *metaFilePath);
// Seeder Implementation Functions
void request_metadata_by_filename(int tracker_socket, const char *metaFilename, FileMetadata *fileMetaData);

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
char *generate_binary_filepath(char *metaFilePath)
{
    size_t metaPathLen = strlen(metaFilePath);
    char *binaryFilePath = malloc(metaPathLen + 1); // +1 for null terminator

    if (binaryFilePath)
    {
        strcpy(binaryFilePath, metaFilePath);

        // Find the .meta extension
        char *extension = strstr(binaryFilePath, ".meta");
        if (extension)
        {
            // Instead of replacing, just terminate the string here
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
} /* ------------------------------------------------------------------------
    Tracker Communication Functions
 ------------------------------------------------------------------------ */
static int connect_to_tracker()
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

    // Connect to the tracker
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting to tracker");
        printf("❌ Connection to Tracker failed. Is it running at %s:%d?\n", TRACKER_IP, TRACKER_PORT);
        close(sockfd);
        return -1;
    }

    printf("✅ Seeder successfully connected to Tracker at %s:%d\n", TRACKER_IP, TRACKER_PORT);
    return sockfd;
}
static void disconnect_from_tracker(int tracker_socket)
{
    close(tracker_socket);
    printf("✅ Seeder successfully disconnected from Tracker\n");
}

/******************************************************************************
SEEDER -> TRACKER FUNCTIONS
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
static void request_participate_seed_by_fileID(int tracker_socket, const char *myIP, const char *myPort, ssize_t fileID)
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
        printf("6) Start Seeding\n");
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

        case 4: {
            // Leech file by fileID
            // 1. Get the metadata and store it in ./storage_downloads
            // 2. Create the bitfield in ./storage_downloads
            // 3. Get Seeder List
            // 4. Start leeching the file from the first person. (KISS)

            // 1 Get metadata and store it
            ssize_t *selectedFileID = malloc(sizeof(ssize_t)); // or size_t if you prefer
            char *metaFilePath = get_metadata_via_cli(tracker_socket, selectedFileID);
            char *binary_filepath;
            if (!metaFilePath){
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
            // 2.5 create binary file
            binary_filepath = generate_binary_filepath(metaFilePath);
            // do we create the binary file using the total bytes in the metadatafile ?
            // Read metadata file to get total bytes needed
            FileMetadata *fileMetadata = malloc(sizeof(FileMetadata));
            if (!fileMetadata)
            {
                perror("Failed to allocate FileMetadata");
                return;
            }

            read_metadata(metaFilePath, fileMetadata);

            // Create binary file with correct size
            FILE *binary_fp = fopen(binary_filepath, "wb");
            if (!binary_fp)
            {
                perror("Failed to create binary file");
                free(fileMetadata);
                return;
            }

            // Seek to totalByte-1 and write a single byte to create file of correct size - sneaky trick :DD
            if (fseek(binary_fp, fileMetadata->totalByte - 1, SEEK_SET) != 0)
            {
                perror("Failed to seek in binary file");
                fclose(binary_fp);
                free(fileMetadata);
                return;
            }
            // Write single byte to set file size
            if (fwrite("", 1, 1, binary_fp) != 1)
            {
                perror("Failed to write to binary file");
                fclose(binary_fp);
                free(fileMetadata);
                return;
            }

            fclose(binary_fp);
            printf("Created empty binary file of size %zd bytes\n", fileMetadata->totalByte);

            // 3 Get Seeder List
            size_t num_seeders = 0;
            PeerInfo *seederList = request_seeder_by_fileID(tracker_socket, *selectedFileID, &num_seeders);

            // 4 Connect to the first seeder if available
            if (seederList && num_seeders > 0)
            {
                // Make a copy of the first seeder info before freeing the list
                PeerInfo firstSeeder = seederList[0];

                printf("\nAvailable seeders (%ld total):\n", num_seeders);

                // test print to see if the seeder List is correct
                for (size_t i = 0; i < num_seeders; i++)
                {
                    printf("%ld) %s:%s\n", i + 1, seederList[i].ip_address, seederList[i].port);
                }

                printf("\nWill connect to first seeder: %s:%s\n", firstSeeder.ip_address, firstSeeder.port);

                // 4.1 Connect to the first seeder and start leeching
                disconnect_from_tracker(tracker_socket);
                int result = leeching(seederList, num_seeders, metaFilePath, bitfieldPath, binary_filepath);

                // Free seederList after using it
                connect_to_tracker();
                printf("works");
                free(seederList);
                break;
            }
            else
            {
                printf("No seeders available for this file.\n");
            }

            // Clean up
            if (bitfieldPath)
                free(bitfieldPath);
            free(fileMetadata);
            free(metaFilePath);
            free(filePath);
            free(selectedFileID);
            break;
        }
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
        case 6:
            disconnect_from_tracker(tracker_socket);

            int listen_fd = setup_seeder_socket(atoi(PEER_1_PORT));
            if (listen_fd < 0)
            {
                // Could not start peer server
                return;
            }

            handle_peer_connection(listen_fd);
            break;
        default:
            printf("Unknown option.\n");
            break;
        }
    }
}
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

/* ------------------------------------------------------------------------
   Seeder -> Leecher FUNCTIONS
------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------
   Main Function - Always Last
------------------------------------------------------------------------ */
int main()
{

    int tracker_socket = connect_to_tracker();

    if (tracker_socket < 0)
    {
        printf("Connection to tracker failed. Please restart program");
        return 1;
    }

    // 3) Show CLI to user: (Register seeds, list files, etc.)
    tracker_cli_loop(tracker_socket, PEER_1_IP, PEER_1_PORT);
    disconnect_from_tracker(tracker_socket);

    return 0;
}
