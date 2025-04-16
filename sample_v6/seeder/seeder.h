#ifndef SEEDER_H
#define SEEDER_H

#include <stddef.h> // for size_t, ssize_t

#include "meta.h"     // FileMetadata
#include "bitfield.h" // bitfield functions
#include "database.h" // FileEntry
#include "leech.h"    // leeching()
#include "seed.h"     // setup_seeder_socket(), handle_peer_connection()

// Constants
#define STORAGE_DIR "./storage_downloads/"
#define CHUNK_DATA_SIZE 1024
#define TRACKER_IP "127.0.0.1"
#define TRACKER_PORT 5555
#define PEER_1_IP "127.0.0.1"
#define PEER_1_PORT "6000"

// Enums
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

typedef enum PeerFSMState{
    Peer_FSM_INIT,
    Peer_FSM_CONNECTING_TO_TRACKER,
    Peer_FSM_TRACKER_CONNECTED,
    Peer_FSM_LISTENING_PEER,
    Peer_FSM_SEEDING,
    Peer_FSM_LEECHING,
    Peer_FSM_ERROR,
    Peer_FSM_CLEANUP,
    Peer_FSM_CLOSING,
    PEER_FSM_HANDLE_EVENT,
} PeerFSMState;

// Structs
typedef struct {
    PeerFSMState current_state;
    int tracker_fd;
    int seeder_fd;
    int leecher_fd;
} PeerContext;

typedef struct {
    char metaFilename[256];
} RequestMetadataBody;

typedef struct {
    TrackerMessageType type;
    ssize_t bodySize;
} TrackerMessageHeader;

typedef struct {
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

// Seeder -> Tracker function declarations
int connect_to_tracker();
void disconnect_from_tracker(int tracker_socket);
void request_metadata_by_filename(int tracker_socket, const char *metaFilename, FileMetadata *fileMetaData);
void request_participate_seed_by_fileID(int tracker_socket, const char *myIP, const char *myPort, ssize_t fileID);
void request_create_seeder(int tracker_socket, const char *myIP, const char *myPort);
void request_create_new_seed(int tracker_socket, const char *binary_file_path);
PeerInfo *request_seeder_by_fileID(int tracker_socket, ssize_t fileID, size_t *num_seeders_out);
char *get_metadata_via_cli(int tracker_socket, ssize_t *selectedFileID);
char *generate_binary_filepath(char *metaFilePath);
void tracker_cli_loop(int tracker_socket, char *ip_address, char *port);
void get_all_available_files(int tracker_socket);

// Main entry point
int main();

// Peer FSM functions
void peer_init();
int peer_connecting_to_tracker();
int peer_listening_peer();
int peer_seeding();
void peer_closing();
void peer_cleanup();
void peer_fsm_handler();

#endif // SEEDER_H
