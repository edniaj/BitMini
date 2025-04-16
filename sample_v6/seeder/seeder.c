#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include "meta.h"
#include "database.h"
#include "bitfield.h"
#include "leech.h"
#include "seed.h"
#include "parser.h"

#define STORAGE_DIR "./storage_downloads/"
#define CHUNK_DATA_SIZE 1024

#define TRACKER_IP "127.0.0.1"
#define TRACKER_PORT 5555

#define PEER_1_IP "127.0.0.1"
#define PEER_1_PORT "6000"

/* ------------------------------------------------------------------------
   Type Definitions
------------------------------------------------------------------------ */
typedef enum TrackerMessageType {
    MSG_REQUEST_ALL_AVAILABLE_SEED = 0,
    MSG_REQUEST_META_DATA,
    MSG_REQUEST_SEEDER_BY_FILEID,
    MSG_REQUEST_CREATE_SEEDER,
    MSG_REQUEST_DELETE_SEEDER,
    MSG_REQUEST_CREATE_NEW_SEED,
    MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID,
    MSG_REQUEST_UNPARTICIPATE_SEED,
    MSG_ACK_CREATE_NEW_SEED,
    MSG_ACK_PARTICIPATE_SEED_BY_FILEID,
    MSG_ACK_SEEDER_BY_FILEID,
    MSG_RESPOND_ERROR
} TrackerMessageType;

typedef struct {
    char metaFilename[256];
} RequestMetadataBody;

typedef struct TrackerMessageHeader {
    TrackerMessageType type;
    ssize_t bodySize;
} TrackerMessageHeader;

typedef struct PeerWithFileID {
    PeerInfo singleSeeder;
    ssize_t fileID;
} PeerWithFileID;

typedef union {
    PeerInfo singleSeeder;
    PeerInfo seederList[64];
    FileMetadata fileMetadata;
    ssize_t fileID;
    PeerWithFileID peerWithFileID;
    char raw[512];
    RequestMetadataBody requestMetaData;
} TrackerMessageBody;

typedef struct {
    TrackerMessageHeader header;
    TrackerMessageBody body;
} TrackerMessage;

/* Function Declarations */
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

/* Helper/Utility Functions */
void request_metadata_by_filename(int tracker_socket, const char *metaFilename, FileMetadata *fileMetaData) {
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_META_DATA;
    msg.header.bodySize = sizeof(RequestMetadataBody);

    strncpy(msg.body.requestMetaData.metaFilename, metaFilename, sizeof(msg.body.requestMetaData.metaFilename) - 1);

    write(tracker_socket, &msg.header, sizeof(msg.header));
    write(tracker_socket, &msg.body.requestMetaData, msg.header.bodySize);

    TrackerMessageHeader respHeader;
    ssize_t n = read(tracker_socket, &respHeader, sizeof(respHeader));
    if (n <= 0) {
        perror("Error reading metadata response header");
        return;
    }

    if (respHeader.type != MSG_REQUEST_META_DATA || respHeader.bodySize != sizeof(FileMetadata)) {
        fprintf(stderr, "Unexpected response from tracker.\n");
        return;
    }

    n = read(tracker_socket, fileMetaData, sizeof(FileMetadata));
    if (n != sizeof(FileMetadata)) {
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

char *generate_binary_filepath(char *metaFilePath) {
    size_t metaPathLen = strlen(metaFilePath);
    char *binaryFilePath = malloc(metaPathLen + 1);

    if (binaryFilePath) {
        strcpy(binaryFilePath, metaFilePath);

        char *extension = strstr(binaryFilePath, ".meta");
        if (extension) {
            *extension = '\0';
            printf("\nbinaryFilePath: %s\n", binaryFilePath);
            return binaryFilePath;
        } else {
            fprintf(stderr, "Error: Metadata file doesn't have expected .meta extension\n");
            free(binaryFilePath);
            return NULL;
        }
    }
    return NULL;
}

/* Tracker Communication Functions */
static int connect_to_tracker() {
    printf("Connecting to Tracker at %s:%d...\n", TRACKER_IP, TRACKER_PORT);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket to tracker");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TRACKER_PORT);

    if (inet_pton(AF_INET, TRACKER_IP, &serv_addr.sin_addr) <= 0) {
        perror("ERROR invalid tracker IP");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting to tracker");
        printf("❌ Connection to Tracker failed. Is it running at %s:%d?\n", TRACKER_IP, TRACKER_PORT);
        close(sockfd);
        return -1;
    }

    printf("✅ Seeder successfully connected to Tracker at %s:%d\n", TRACKER_IP, TRACKER_PORT);
    return sockfd;
}

static void disconnect_from_tracker(int tracker_socket) {
    close(tracker_socket);
    printf("✅ Seeder successfully disconnected from Tracker\n");
}

/* Seeder -> Tracker Functions */
static void request_create_new_seed(int tracker_socket, const char *binary_file_path) {
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
    if (bytes_written != sizeof(msg.header)) {
        fprintf(stderr, "Failed to send message header\n");
        return;
    }

    bytes_written = write(tracker_socket, &msg.body.fileMetadata, msg.header.bodySize);
    if (bytes_written != msg.header.bodySize) {
        fprintf(stderr, "Failed to send message body\n");
        return;
    }

    TrackerMessageHeader ack_header;
    ssize_t bytes_read = read(tracker_socket, &ack_header, sizeof(ack_header));
    if (bytes_read != sizeof(ack_header)) {
        fprintf(stderr, "Failed to read ACK header\n");
        return;
    }

    if (ack_header.type != MSG_ACK_CREATE_NEW_SEED) {
        fprintf(stderr, "Did not receive MSG_ACK_CREATE_NEW_SEED.\n");
        return;
    }

    if (ack_header.bodySize != sizeof(ssize_t)) {
        fprintf(stderr, "ACK bodySize mismatch.\n");
        return;
    }

    ssize_t newFileID;
    bytes_read = read(tracker_socket, &newFileID, sizeof(newFileID));
    if (bytes_read != sizeof(newFileID)) {
        fprintf(stderr, "Failed to read file ID\n");
        return;
    }

    printf("Tracker assigned fileID: %zd\n", newFileID);

    char *metaPath = generate_metafile_filepath_with_id(newFileID, binary_file_path);
    if (!metaPath) {
        fprintf(stderr, "Failed to generate .meta path.\n");
        return;
    }

    fileMeta.fileID = newFileID;

    if (write_metadata(metaPath, &fileMeta) != 0) {
        fprintf(stderr, "Failed to write metadata: %s\n", metaPath);
        free(metaPath);
        return;
    }

    printf("Wrote metadata file: %s\n", metaPath);

    char *bitfieldPath = generate_bitfield_filepath_with_id(newFileID, binary_file_path);
    if (!bitfieldPath) {
        fprintf(stderr, "Failed to generate .bitfield path.\n");
        free(metaPath);
        return;
    }

    printf("bitfield path : %s\n", bitfieldPath);
    create_filled_bitfield(metaPath, bitfieldPath);

    free(metaPath);
    free(bitfieldPath);
}

static PeerInfo *request_seeder_by_fileID(int tracker_socket, ssize_t fileID, size_t *num_seeders_out) {
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_SEEDER_BY_FILEID;
    msg.header.bodySize = sizeof(ssize_t);
    msg.body.fileID = fileID;

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0) {
        perror("ERROR writing header (REQUEST_SEEDER_BY_FILEID)");
        return NULL;
    }
    if (write(tracker_socket, &msg.body.fileID, msg.header.bodySize) < 0) {
        perror("ERROR writing fileID body");
        return NULL;
    }

    TrackerMessageHeader ack_header;
    if (read(tracker_socket, &ack_header, sizeof(ack_header)) <= 0) {
        perror("ERROR reading ack header (REQUEST_SEEDER_BY_FILEID)");
        return NULL;
    }

    if (ack_header.type != MSG_ACK_SEEDER_BY_FILEID) {
        fprintf(stderr, "Expected MSG_ACK_SEEDER_BY_FILEID, got %d\n", ack_header.type);
        return NULL;
    }

    if (ack_header.bodySize == 0) {
        printf("No seeders for fileID=%zd.\n", fileID);
        return NULL;
    }

    size_t num_seeders = ack_header.bodySize / sizeof(PeerInfo);
    PeerInfo *seederList = calloc(num_seeders, sizeof(PeerInfo));
    if (!seederList) {
        perror("calloc failed");
        return NULL;
    }

    if (read(tracker_socket, seederList, ack_header.bodySize) < 0) {
        perror("ERROR reading seeder list");
        free(seederList);
        return NULL;
    }

    printf("Received %zu seeders for fileID=%zd:\n", num_seeders, fileID);
    for (size_t i = 0; i < num_seeders; i++) {
        printf("  -> %s:%s\n", seederList[i].ip_address, seederList[i].port);
    }

    if (num_seeders_out) {
        *num_seeders_out = num_seeders;
    }

    return seederList;
}

static void request_participate_seed_by_fileID(int tracker_socket, const char *myIP, const char *myPort, ssize_t fileID) {
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID;
    msg.header.bodySize = sizeof(PeerWithFileID);

    strncpy(msg.body.peerWithFileID.singleSeeder.ip_address, myIP,
            sizeof(msg.body.peerWithFileID.singleSeeder.ip_address) - 1);
    strncpy(msg.body.peerWithFileID.singleSeeder.port, myPort,
            sizeof(msg.body.peerWithFileID.singleSeeder.port) - 1);
    msg.body.peerWithFileID.fileID = fileID;

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0) {
        perror("ERROR writing header (PARTICIPATE_SEED_BY_FILEID)");
        return;
    }
    if (write(tracker_socket, &msg.body.peerWithFileID, msg.header.bodySize) < 0) {
        perror("ERROR writing PeerWithFileID body");
        return;
    }

    TrackerMessageHeader ack_header;
    if (read(tracker_socket, &ack_header, sizeof(ack_header)) <= 0) {
        perror("ERROR reading ack header (PARTICIPATE_SEED_BY_FILEID)");
        return;
    }

    if (ack_header.type == MSG_ACK_PARTICIPATE_SEED_BY_FILEID) {
        printf("Successfully registered as a seeder for fileID %zd.\n", fileID);
        if (ack_header.bodySize > 0) {
            char buffer[256];
            ssize_t n = read(tracker_socket, buffer, ack_header.bodySize < 256 ? ack_header.bodySize : 255);
            if (n > 0) {
                buffer[n] = '\0';
                printf("Tracker says: %s\n", buffer);
            }
        }
    } else {
        fprintf(stderr, "Tracker did not ACK participation. Type=%d\n", ack_header.type);
        if (ack_header.bodySize > 0) {
            char buffer[512];
            ssize_t n = read(tracker_socket, buffer, ack_header.bodySize < 512 ? ack_header.bodySize : 512);
            if (n > 0) {
                buffer[n] = '\0';
                fprintf(stderr, "Tracker error: %s\n", buffer);
            }
        }
    }
}

static void request_create_seeder(int tracker_socket, const char *myIP, const char *myPort) {
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.type = MSG_REQUEST_CREATE_SEEDER;
    msg.header.bodySize = sizeof(PeerInfo);

    strncpy(msg.body.singleSeeder.ip_address, myIP,
            sizeof(msg.body.singleSeeder.ip_address) - 1);
    strncpy(msg.body.singleSeeder.port, myPort,
            sizeof(msg.body.singleSeeder.port) - 1);

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0) {
        perror("ERROR writing tracker header (CREATE_SEEDER)");
        return;
    }
    if (write(tracker_socket, &msg.body.singleSeeder, msg.header.bodySize) < 0) {
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

static void tracker_cli_loop(int tracker_socket, char *ip_address, char *port) {
    char *input = malloc(250);
    if (!input) {
        perror("Failed to allocate input buffer");
        return;
    }

    while (1) {
        printf("\n--- Tracker CLI Options ---\n");
        printf("1) Register as seeder\n");
        printf("2) Get all available files\n");
        printf("3) Create new seed\n");
        printf("4) Leech file by fileID\n");
        printf("5) Participate seeding by fileID\n");
        printf("6) Start Seeding\n");
        printf("7) Execute command (e.g., BLOCK FILENAME ...)\n");
        printf("0) Exit Tracker\n");
        printf("Choose an option: ");

        if (!fgets(input, 250, stdin)) {
            printf("Error reading input.\n");
            continue;
        }

        input[strcspn(input, "\n")] = 0;

        int choice = atoi(input);

        switch (choice) {
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
            if (!fgets(input, 250, stdin)) {
                printf("Error reading directory.\n");
                break;
            }
            input[strcspn(input, "\n")] = 0;
            request_create_new_seed(tracker_socket, input);
            break;

        case 4: {
            ssize_t selectedFileID;
            char *metaFilePath = get_metadata_via_cli(tracker_socket, &selectedFileID);
            char *binary_filepath;
            if (!metaFilePath) {
                break;
            }
            printf("\nmetaFilePath:%s\n", metaFilePath);

            char *filePath = malloc(strlen(metaFilePath) + 1);
            char *lastSlash = strrchr(metaFilePath, '/');
            char *directory = NULL;
            char *baseName = NULL;

            if (!lastSlash) {
                directory = strdup(".");
                baseName = metaFilePath;
            } else {
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
            if (metaFilePath) {
                size_t metaPathLen = strlen(metaFilePath);
                bitfieldPath = malloc(metaPathLen + 4 + 1);

                if (bitfieldPath) {
                    strcpy(bitfieldPath, metaFilePath);
                    char *extension = strstr(bitfieldPath, ".meta");
                    if (extension) {
                        strcpy(extension, ".bitfield");
                        create_empty_bitfield(metaFilePath, bitfieldPath);
                        printf("\nbitfieldPath:%s\n", bitfieldPath);
                    } else {
                        fprintf(stderr, "Error: Metadata file doesn't have expected .meta extension\n");
                        free(bitfieldPath);
                        bitfieldPath = NULL;
                    }
                }
            }

            binary_filepath = generate_binary_filepath(metaFilePath);
            FileMetadata *fileMetadata = malloc(sizeof(FileMetadata));
            if (!fileMetadata) {
                perror("Failed to allocate FileMetadata");
                free(metaFilePath);
                if (bitfieldPath) free(bitfieldPath);
                return;
            }

            read_metadata(metaFilePath, fileMetadata);

            FILE *binary_fp = fopen(binary_filepath, "wb");
            if (!binary_fp) {
                perror("Failed to create binary file");
                free(fileMetadata);
                free(metaFilePath);
                if (bitfieldPath) free(bitfieldPath);
                free(binary_filepath);
                return;
            }

            if (fseek(binary_fp, fileMetadata->totalByte - 1, SEEK_SET) != 0) {
                perror("Failed to seek in binary file");
                fclose(binary_fp);
                free(fileMetadata);
                free(metaFilePath);
                if (bitfieldPath) free(bitfieldPath);
                free(binary_filepath);
                return;
            }

            if (fwrite("", 1, 1, binary_fp) != 1) {
                perror("Failed to write to binary file");
                fclose(binary_fp);
                free(fileMetadata);
                free(metaFilePath);
                if (bitfieldPath) free(bitfieldPath);
                free(binary_filepath);
                return;
            }

            fclose(binary_fp);
            printf("Created empty binary file of size %zd bytes\n", fileMetadata->totalByte);

            size_t num_seeders = 0;
            PeerInfo *seederList = request_seeder_by_fileID(tracker_socket, selectedFileID, &num_seeders);

            if (seederList && num_seeders > 0) {
                PeerInfo firstSeeder = seederList[0];

                printf("\nAvailable seeders (%ld total):\n", num_seeders);
                for (size_t i = 0; i < num_seeders; i++) {
                    printf("%ld) %s:%s\n", i + 1, seederList[i].ip_address, seederList[i].port);
                }

                printf("\nWill connect to first seeder: %s:%s\n", firstSeeder.ip_address, firstSeeder.port);

                disconnect_from_tracker(tracker_socket);
                int result = leeching(seederList, num_seeders, metaFilePath, bitfieldPath, binary_filepath);

                tracker_socket = connect_to_tracker();
                printf("Reconnected to tracker\n");
                free(seederList);
            } else {
                printf("No seeders available for this file.\n");
            }

            if (bitfieldPath) free(bitfieldPath);
            free(fileMetadata);
            free(metaFilePath);
            free(filePath);
            free(binary_filepath);
            break;
        }

        case 5:
            printf("\nEnter fileID:\n");
            if (!fgets(input, 250, stdin)) {
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
            if (listen_fd < 0) {
                return;
            }
            handle_peer_connection(listen_fd);
            break;

        case 7:
            printf("Enter command (e.g., BLOCK FILENAME \"file.jpg\" FROM CHINA TO AMERICA):\n");
            if (!fgets(input, 250, stdin)) {
                printf("Error reading command\n");
                break;
            }
            input[strcspn(input, "\n")] = 0;
            ASTNode *ast = parse_command(input);
            if (ast) {
                execute_ast(ast, tracker_socket, NULL);
                free_ast(ast);
            } else {
                printf("Invalid command\n");
            }
            break;

        default:
            printf("Unknown option.\n");
            break;
        }
    }
}

static void get_all_available_files(int tracker_socket) {
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.type = MSG_REQUEST_ALL_AVAILABLE_SEED;
    msg.header.bodySize = 0;

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0) {
        perror("ERROR writing header (ALL_AVAILABLE_SEED)");
        return;
    }

    size_t fileCount = 0;
    ssize_t rc = read(tracker_socket, &fileCount, sizeof(fileCount));
    if (rc <= 0) {
        char textBuf[256];
        memset(textBuf, 0, sizeof(textBuf));
        rc = read(tracker_socket, textBuf, sizeof(textBuf) - 1);
        if (rc > 0) {
            printf("Tracker says: %s\n", textBuf);
        } else {
            perror("ERROR reading from tracker (ALL_AVAILABLE_SEED)");
        }
        return;
    }

    printf("Tracker says there are %zu files.\n", fileCount);
    for (size_t i = 0; i < fileCount; i++) {
        FileEntry entry;
        rc = read(tracker_socket, &entry, sizeof(entry));
        if (rc <= 0) {
            perror("ERROR reading file entry from tracker");
            return;
        }
        printf(" -> FileID: %04zd TotalByte: %zd MetaFile: %s\n",
               entry.fileID, entry.totalBytes, entry.metaFilename);
    }
}

char *get_metadata_via_cli(int tracker_socket, ssize_t *selectedFileID) {
    TrackerMessage msg;
    memset(&msg, 0, sizeof(msg));

    msg.header.type = MSG_REQUEST_ALL_AVAILABLE_SEED;
    msg.header.bodySize = 0;

    if (write(tracker_socket, &msg.header, sizeof(msg.header)) < 0) {
        perror("ERROR writing header (ALL_AVAILABLE_SEED)");
        return NULL;
    }

    size_t fileCount = 0;
    ssize_t rc = read(tracker_socket, &fileCount, sizeof(fileCount));
    if (rc <= 0) {
        char textBuf[256] = {0};
        rc = read(tracker_socket, textBuf, sizeof(textBuf) - 1);
        if (rc > 0)
            printf("Tracker says: %s\n", textBuf);
        else
            perror("ERROR reading from tracker (ALL_AVAILABLE_SEED)");
        return NULL;
    }

    FileEntry *entries = malloc(fileCount * sizeof(FileEntry));
    if (!entries) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    printf("Tracker says there are %zu files.\n", fileCount);

    for (size_t i = 0; i < fileCount; i++) {
        rc = read(tracker_socket, &entries[i], sizeof(FileEntry));
        if (rc <= 0) {
            perror("ERROR reading file entry from tracker");
            free(entries);
            return NULL;
        }

        printf(" -> FileID: %04zd TotalByte: %zd MetaFile: %s\n",
               entries[i].fileID, entries[i].totalBytes, entries[i].metaFilename);
    }

    printf("\nEnter fileID to print its metaFilename:\n");
    char input[256];
    if (!fgets(input, sizeof(input), stdin)) {
        fprintf(stderr, "Error reading input.\n");
        free(entries);
        return NULL;
    }
    input[strcspn(input, "\n")] = 0;
    *selectedFileID = atoi(input);

    const char *metaFilename = NULL;
    for (size_t i = 0; i < fileCount; i++) {
        if (entries[i].fileID == *selectedFileID) {
            metaFilename = entries[i].metaFilename;
            break;
        }
    }

    if (!metaFilename) {
        printf("No file found with ID %zd\n", *selectedFileID);
        free(entries);
        return NULL;
    }

    printf("📄 metaFilename for fileID %zd: %s\n", *selectedFileID, metaFilename);

    FileMetadata fileMetaData;
    request_metadata_by_filename(tracker_socket, metaFilename, &fileMetaData);

    char *metafile_directory = malloc(256);
    if (!metafile_directory) {
        perror("malloc failed");
        free(entries);
        return NULL;
    }
    snprintf(metafile_directory, 256, "./storage_downloads/%s", metaFilename);

    FILE *metafile_fp = fopen(metafile_directory, "wb");
    if (!metafile_fp) {
        perror("Failed to open file for writing");
        free(metafile_directory);
        free(entries);
        return NULL;
    }

    if (fwrite(&fileMetaData, sizeof(FileMetadata), 1, metafile_fp) != 1) {
        perror("Failed to write metadata to file");
        fclose(metafile_fp);
        free(metafile_directory);
        free(entries);
        return NULL;
    }

    fclose(metafile_fp);
    free(entries);

    *selectedFileID = fileMetaData.fileID;

    return metafile_directory;
}

/* Main Function */
int main() {
    int tracker_socket = connect_to_tracker();

    if (tracker_socket < 0) {
        printf("Connection to tracker failed. Please restart program\n");
        return 1;
    }

    tracker_cli_loop(tracker_socket, PEER_1_IP, PEER_1_PORT);
    disconnect_from_tracker(tracker_socket);

    return 0;
}