#ifndef TRACKER_H
#define TRACKER_H

#include <stddef.h> // for ssize_t
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
#include "tracker.h"
#include "meta.h"
#include "peerCommunication.h"
#include "seed.h"
// Forward declaration for FileMetadata from database.h
typedef struct FileMetadata FileMetadata;

/* --------------------------------------------------------------------------
   🔹 Constants
   -------------------------------------------------------------------------- */
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
    MSG_REQUEST_SEEDER_BY_FILEID,
    MSG_REQUEST_CREATE_SEEDER,
    MSG_REQUEST_DELETE_SEEDER,
    MSG_REQUEST_CREATE_NEW_SEED,
    MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID,
    MSG_REQUEST_UNPARTICIPATE_SEED,
    MSG_ACK_CREATE_NEW_SEED,
    MSG_ACK_PARTICIPATE_SEED_BY_FILEID,
    MSG_ACK_SEEDER_BY_FILEID,
    MSG_RESPOND_ERROR,
    MSG_ACK_FILEHASH_BLOCKED,
    MSG_ACK_IP_BLOCKED
} TrackerMessageType;

/* --------------------------------------------------------------------------
   🔹 FSM States and Events
   -------------------------------------------------------------------------- */
typedef enum TrackerFSMState {
    Tracker_FSM_INIT = 0,
    TRACKER_FSM_COMMAND_MODE,
    TRACKER_FSM_SET_UP_LISTENING,
    Tracker_FSM_LISTENING_PEER,
    Tracker_FSM_LISTENING_EVENT,
    Tracker_FSM_HANDLE_EVENT,
    Tracker_FSM_ERROR,
    Tracker_FSM_CLEANUP,
    Tracker_FSM_CLOSING
} TrackerFSMState;

typedef enum FSM_TRACKER_EVENT {
    FSM_EVENT_REQUEST_ALL_AVAILABLE_SEED = 0,
    FSM_EVENT_REQUEST_META_DATA,
    FSM_EVENT_REQUEST_SEEDER_BY_FILEID,
    FSM_EVENT_REQUEST_CREATE_SEEDER,
    FSM_EVENT_REQUEST_DELETE_SEEDER,
    FSM_EVENT_REQUEST_CREATE_NEW_SEED,
    FSM_EVENT_REQUEST_PARTICIPATE_SEED_BY_FILEID,
    FSM_EVENT_REQUEST_UNPARTICIPATE_SEED,
    FSM_EVENT_ACK_CREATE_NEW_SEED,
    FSM_EVENT_ACK_PARTICIPATE_SEED_BY_FILEID,
    FSM_EVENT_ACK_SEEDER_BY_FILEID,
    FSM_EVENT_RESPOND_ERROR,
    FSM_EVENT_NULL
} FSM_TRACKER_EVENT;

/* --------------------------------------------------------------------------
   🔹 Structures and Types
   -------------------------------------------------------------------------- */
/*
We need to use the TrackerFSMState enum to track the current state of the tracker and trigger the correct function
*/
typedef struct {
    enum TrackerFSMState current_state;
    int listen_socket;
    int client_socket;
    PeerInfo client_peer;    
} TrackerContext;

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

typedef struct
{
    char metaFilename[256]; // or whatever size you use
} RequestMetadataBody;

typedef union
{
    PeerInfo singleSeeder;     // For REGISTER / UNREGISTER
    PeerInfo seederList[64];   // For returning a list of seeders
    FileMetadata fileMetadata; // For CREATE_NEW_SEED
    ssize_t fileID;            // For simple queries
    PeerWithFileID peerWithFileID;
    RequestMetadataBody requestMetaData; //
    char raw[512];                       // fallback
} TrackerMessageBody;

typedef struct
{
    TrackerMessageHeader header;
    TrackerMessageBody body;
} TrackerMessage;

/* --------------------------------------------------------------------------
   🔹 Function Declarations
   -------------------------------------------------------------------------- */
// Core tracker functions
void tracker_init(void);
void tracker_event_handler(FSM_TRACKER_EVENT event);
void tracker_set_up_listening();
void tracker_command_mode();
void tracker_listening_peer(void);
void tracker_listening_event(void);
void tracker_fsm_handler(void);
void tracker_error_handler(void);
void tracker_closing(void);
void tracker_cleanup(void);
void tracker_close_peer(void);
int read_header(void);
int read_body(void);

// Utility functions
void init_seeders(void);
int setup_server(void);
FSM_TRACKER_EVENT map_msg_type_to_fsm_event(TrackerMessageType type);
void exit_success(void);

// Peer management functions
PeerInfo *find_peer(const PeerInfo *p);
PeerInfo *add_peer(const PeerInfo *p);
void remove_peer(PeerInfo *p);
int add_seeder_to_file(ssize_t fileID, PeerInfo *p);
int remove_seeder_from_file(ssize_t fileID, PeerInfo *p);

// Request handler functions
void handle_create_seeder(int client_socket, const PeerInfo *p);
void handle_create_new_seed(int client_socket, const FileMetadata *meta);
void handle_request_all_available_files(int client_socket);
void handle_request_participate_by_fileID(int client_socket, const PeerWithFileID *peerWithFileID);
void handle_request_seeder_by_fileID(int client_socket, ssize_t fileID);
void handle_request_metadata(int client_socket, const RequestMetadataBody *req);

#endif // TRACKER_H