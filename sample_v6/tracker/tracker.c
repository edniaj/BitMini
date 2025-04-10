#include "tracker.h"
#define BUFFER_SIZE (1024 * 5)
#define SERVER_PORT 5555
#define SERVER_IP "127.0.0.1"

#define MAX_FILES 10000
#define MAX_SEEDERS_PER_FILE 64
#define MAX_SEEDERS 1000

// urgent : Theres a bug in the lookup of binary files.






/* --------------------------------------------------------------------------
   🔹 Message Types
   -------------------------------------------------------------------------- */





static PeerInfo list_seeders[MAX_SEEDERS];
static PeerInfo *file_to_seeders[MAX_FILES][MAX_SEEDERS_PER_FILE];



/* --------------------------------------------------------------------------
   🔹 Global Variables
   -------------------------------------------------------------------------- */
static TrackerContext* ctx;
static TrackerMessageHeader* header;
static TrackerMessageBody* body;
static int listen_socketfd; // Global variable to hold the listening socket

// Forward declarations for any missing functions
void exit_success(void) {
    exit(EXIT_SUCCESS);
}

/* --------------------------------------------------------------------------
   🔹 Function Implementations
   -------------------------------------------------------------------------- */

/* Initialization functions */
void init_seeders(void)
{
    memset(list_seeders, 0, sizeof(list_seeders));
    memset(file_to_seeders, 0, sizeof(file_to_seeders));
}

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
        ctx->current_state = Tracker_FSM_ERROR;
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
        ctx->current_state = Tracker_FSM_ERROR;
        exit(EXIT_FAILURE);
    }

    if (listen(listen_socketfd, 10) < 0)
    {
        perror("ERROR on listen");
        ctx->current_state = Tracker_FSM_ERROR;
        exit(EXIT_FAILURE);
    }

    printf("✅ Tracker listening on %s:%d\n", SERVER_IP, SERVER_PORT);
    return listen_socketfd;
}

/* Peer management functions */
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

/* Request handler functions */
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

void handle_request_all_available_files(int client_socket)
{
    size_t fileCount = 0; //placeholder
    FileEntry *fileList = load_file_entries(&fileCount);
    printf("fileCount: %zd\n", fileCount);
    printf("\nAvailable Files:\n");
    printf("---------------------------\n");

    for (size_t i = 0; i < fileCount; i++) {
        printf("index i : %ld" ,i);
        printf("File ID: %04zd, Total Bytes: %zd, Meta File: %s\n",fileList[i].fileID, fileList[i].totalBytes, fileList[i].metaFilename);
    }

    if (!fileList || fileCount == 0)
    {
        char error_msg[] = "No available files.\n";
        write(client_socket, error_msg, strlen(error_msg));
        return;
    }
    printf("Sending fileCount: %zu\n", fileCount);
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

    // Check if socket is still valid before sending
    int error = 0;
    socklen_t len = sizeof(error);
    int retval = getsockopt(client_socket, SOL_SOCKET, SO_ERROR, &error, &len);
    
    if (retval != 0 || error != 0) {
        printf("Socket error detected before sending ACK (error=%d)\n", error);
        // Socket is no longer valid
        ctx->current_state = Tracker_FSM_LISTENING_PEER;
        return;
    }

    printf("Socket is valid, preparing to send ACK\n");
    

    // Step 3: Acknowledge with new fileID
    TrackerMessageHeader ack_header;
    memset(&ack_header, 0, sizeof(ack_header));
    ack_header.type = MSG_ACK_CREATE_NEW_SEED;
    ack_header.bodySize = sizeof(ssize_t);

    printf("sending ack ");
    printf("ctx->client_socket: %d\n", ctx->client_socket);
    printf("Sending ACK header: type=%d, bodySize=%zd\n", ack_header.type, ack_header.bodySize);
    write(ctx->client_socket, &ack_header, sizeof(ack_header));
    write(ctx->client_socket, &fileID, sizeof(ssize_t)); // send full TrackerMessageBody
    printf("done  - sending ack \n\n ");
}

void handle_request_participate_by_fileID(int client_socket, const PeerWithFileID *peerWithFileID)
{
    // 1) Ensure the peer is in the master array
    PeerInfo *existingPeer = find_peer(&peerWithFileID->singleSeeder);
    if (!existingPeer)
    {
        // Peer must call "create_seeder" first
        printf("Peer not in master list, requester must register seeder first.\n%s\n %s\n %zd", peerWithFileID->singleSeeder.ip_address, peerWithFileID->singleSeeder.port, peerWithFileID->fileID);

        TrackerMessageHeader ackHeader;
        TrackerMessageBody ackBody;

        ackHeader.bodySize = sizeof(ackBody.raw);
        ackHeader.type = MSG_RESPOND_ERROR;
        strcpy(ackBody.raw, "You must register as a seeder first.\n");
        write(client_socket, &ackHeader, sizeof(TrackerMessageHeader));
        write(client_socket, &ackBody, sizeof(ackBody));
        return;
    }

    // 2) Add the peer to file_to_seeders
    ssize_t fileID = peerWithFileID->fileID;
    int addResult = add_seeder_to_file(fileID, existingPeer);
    if (addResult == 0)
    {
        // 0 means success
        printf("Peer %s:%s added as seeder for fileID=%zd\n",
               existingPeer->ip_address, existingPeer->port, fileID);

        // Optionally send a short ACK message or
        // a formal TrackerMessage with MSG_ACK_PARTICIPATE_SEED_BY_FILEID
        TrackerMessageHeader ack;
        memset(&ack, 0, sizeof(ack));
        ack.type = MSG_ACK_PARTICIPATE_SEED_BY_FILEID;
        ack.bodySize = 0; // no body or you can send a short text

        write(client_socket, &ack, sizeof(ack));
    }
    else if (addResult == 1)
    {
        // 1 means "already present"
        printf("Peer %s:%s is already seeding fileID=%zd\n",
               existingPeer->ip_address, existingPeer->port, fileID);

        const char *already_msg = "Already participating in this file.\n";
        write(client_socket, already_msg, strlen(already_msg));
    }
    else
    {
        // -1 means no space
        fprintf(stderr, "No space to add seeder in fileID=%zd\n", fileID);
        const char *fail_msg = "No space in this file's seeder list.\n";
        write(client_socket, fail_msg, strlen(fail_msg));
    }
}

void handle_request_seeder_by_fileID(int client_socket, ssize_t fileID)
{
    // 1) Gather seeders in a local array
    PeerInfo seederList[MAX_SEEDERS_PER_FILE];
    memset(seederList, 0, sizeof(seederList));

    size_t count = 0;
    for (int i = 0; i < MAX_SEEDERS_PER_FILE; i++)
    {
        PeerInfo *p = file_to_seeders[fileID][i];
        if (p != NULL)
        {
            // Copy the actual PeerInfo struct
            seederList[count] = *p;
            count++;
            if (count >= MAX_SEEDERS_PER_FILE)
                break;
        }
    }

    // 2) Create a response message
    TrackerMessageHeader ackHeader;
    memset(&ackHeader, 0, sizeof(ackHeader));
    ackHeader.type = MSG_ACK_SEEDER_BY_FILEID;
    // The body will be `count` PeerInfo structs
    ackHeader.bodySize = count * sizeof(PeerInfo);

    // 3) Send the header
    ssize_t written = write(client_socket, &ackHeader, sizeof(ackHeader));
    if (written < 0)
    {
        ctx->current_state = Tracker_FSM_ERROR;
        perror("ERROR writing MSG_ACK_SEEDER_BY_FILEID header");
        return;
    }

    // 4) Send the body (the array of PeerInfo)
    if (count > 0)
    {
        written = write(client_socket, seederList, ackHeader.bodySize);
        if (written < 0)
        {
            ctx->current_state = Tracker_FSM_ERROR;
            perror("ERROR writing MSG_ACK_SEEDER_BY_FILEID body");
            return;
        }
    }

    printf("Sent %zd seeders for fileID=%zd\n", count, fileID);
    ctx->current_state = Tracker_FSM_LISTENING_EVENT;
}

void handle_request_metadata(int client_socket, const RequestMetadataBody *req)
{
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "./records/%s", req->metaFilename);
    printf("filepath: %s", filepath);
    FileMetadata meta;
    if (load_single_metadata(filepath, &meta) != 0)
    {
        TrackerMessageHeader errHeader = {MSG_RESPOND_ERROR, 0};
        write(client_socket, &errHeader, sizeof(errHeader));
        return;
    }

    TrackerMessageHeader respHeader;
    respHeader.type = MSG_REQUEST_META_DATA;
    respHeader.bodySize = sizeof(FileMetadata);

    write(client_socket, &respHeader, sizeof(respHeader));
    write(client_socket, &meta, sizeof(meta));

    printf("✅ Sent metadata for file: %s\n", req->metaFilename);
}

int read_header() {
    printf("Attempting to read header of size %zu bytes...\n", sizeof(TrackerMessageHeader));
    
    ssize_t bytes_read = read(ctx->client_socket, header, sizeof(TrackerMessageHeader));
    
    printf("Header read result: %zd bytes (expected %zu)\n", bytes_read, sizeof(TrackerMessageHeader));
    
    // IMPORTANT: Only check errno if bytes_read < 0
    if (bytes_read < 0) {
        printf("ERROR: Header read failed (bytes_read = %zd)\n", bytes_read);
        if (errno == ECONNRESET) {
            printf("Detected ECONNRESET: Peer disconnected abruptly.\n");
            ctx->current_state = Tracker_FSM_LISTENING_PEER;
            return 1;
        } else {
            printf("Socket error: %s (errno=%d)\n", strerror(errno), errno);
            ctx->current_state = Tracker_FSM_ERROR;
            return 1;
        }
        return 1;
    }
    
    if (bytes_read == 0) {
        printf("Graceful disconnection: Client closed connection (bytes_read = 0)\n");
        ctx->current_state = Tracker_FSM_LISTENING_PEER;
        return 1;
    }
    
    if (bytes_read < sizeof(TrackerMessageHeader)) {
        printf("WARNING: Incomplete header read! Got %zd of %zu bytes\n", 
               bytes_read, sizeof(TrackerMessageHeader));
        ctx->current_state = Tracker_FSM_ERROR;
        return 1;
    }
    
    // Print header contents for debugging
    printf("Header read successfully: Type=%d, BodySize=%zd\n", 
           header->type, header->bodySize);
    
    return 0;
}
int read_body() {
    if (header->bodySize > 0) {
        size_t total_bytes_read = 0;
        char* buffer_position = (char*)body; // Cast to char* for pointer arithmetic
        
        // Keep reading until we have all the bytes or encounter an error
        while (total_bytes_read < header->bodySize) {
            ssize_t bytes_read = read(ctx->client_socket, 
                                      buffer_position + total_bytes_read, 
                                      header->bodySize - total_bytes_read);
            
            if (bytes_read < 0) {
                if (errno == ECONNRESET) {
                    printf("Peer disconnected abruptly (Connection reset).\n");
                    ctx->current_state = Tracker_FSM_LISTENING_PEER;
                    return 1;
                }
                perror("ERROR reading body");
                ctx->current_state = Tracker_FSM_ERROR;
                return 1;
            }
            
            if (bytes_read == 0) {
                printf("Client disconnected during body read.\n");
                ctx->current_state = Tracker_FSM_LISTENING_PEER;
                return 1;
            }
            
            total_bytes_read += bytes_read;
        }
    }
    
    return 0; // Success
}
void tracker_event_handler(FSM_TRACKER_EVENT event) {

    /* Read Header and Body was handled in tracker_listening_event() */
    switch (event)
    {
    case FSM_EVENT_REQUEST_CREATE_SEEDER:
        if (header->bodySize == sizeof(PeerInfo)) {            
            handle_create_seeder(ctx->client_socket, &(body->singleSeeder));
            ctx->current_state = Tracker_FSM_LISTENING_EVENT;
        } else {
            char err[] = "Invalid body size for CREATE_SEEDER.\n";
            ctx->current_state = Tracker_FSM_ERROR;
            write(ctx->client_socket, err, strlen(err));
        }
        
        break;

    case FSM_EVENT_REQUEST_ALL_AVAILABLE_SEED:
        printf("requesting all seed");
        handle_request_all_available_files(ctx->client_socket);
        ctx->current_state = Tracker_FSM_LISTENING_EVENT;

        break;

    case FSM_EVENT_REQUEST_CREATE_NEW_SEED:
        
        if (header->bodySize == sizeof(FileMetadata)) {
            handle_create_new_seed(ctx->client_socket, &(body->fileMetadata));
            ctx->current_state = Tracker_FSM_LISTENING_EVENT;
        } else {
            printf("invalid body size for CREATE_NEW_SEED.\n");
            char err[] = "Invalid body size for CREATE_NEW_SEED.\n";
            write(ctx->client_socket, err, strlen(err));
            ctx->current_state = Tracker_FSM_ERROR;
        }
        break;

    case FSM_EVENT_REQUEST_PARTICIPATE_SEED_BY_FILEID:
        printf("hello");
        
        handle_request_participate_by_fileID(ctx->client_socket, &(body->peerWithFileID));
        ctx->current_state = Tracker_FSM_LISTENING_EVENT;
        break;

    case FSM_EVENT_REQUEST_SEEDER_BY_FILEID:
        
        handle_request_seeder_by_fileID(ctx->client_socket, body->fileID);
        break;

    case FSM_EVENT_REQUEST_META_DATA:
        
        if (header->bodySize == sizeof(RequestMetadataBody)) {
            handle_request_metadata(ctx->client_socket, &(body->requestMetaData));
            ctx->current_state = Tracker_FSM_LISTENING_EVENT;
        } else {
            char err[] = "Invalid body size for REQUEST_META_DATA.\n";
            write(ctx->client_socket, err, strlen(err));
        }
        break;

    default: {
        char error_msg[] = "Unknown or unimplemented FSM event.\n";
        write(ctx->client_socket, error_msg, strlen(error_msg));
        fprintf(stderr, "⚠️ Unknown FSM event: %d\n", event);
        break;
        }
    }

    

}

void tracker_listening_peer(){
    
    struct sockaddr_in client_addr;
    socklen_t client_addr_length = sizeof(client_addr);

    int client_socket = accept(listen_socketfd,
                               (struct sockaddr *)&client_addr,
                               &client_addr_length);
    if (client_socket < 0)
    {
        perror("ERROR accepting connection");
        ctx->current_state = Tracker_FSM_ERROR;
    }

    printf("✅ New connection established.\n");
    ctx->client_socket = client_socket;
    ctx->current_state = Tracker_FSM_LISTENING_EVENT;
    return;
}

void tracker_listening_event()
{

        

        // 1) Read header
        if (read_header() == 1 ) {
            printf("ending read_header()");
            // there was some special case and we should stop this function and move to the next state.
            return;
        }

        if (read_body() ==1 ) {
            // there was some special case and we should stop this function and move to the next state.
            printf("read_body() ==1 ");
            return ;
        }


        ctx->current_state = Tracker_FSM_HANDLE_EVENT;
    

}

FSM_TRACKER_EVENT map_msg_type_to_fsm_event(TrackerMessageType type) {
    switch (type) {
        case MSG_REQUEST_ALL_AVAILABLE_SEED:       return FSM_EVENT_REQUEST_ALL_AVAILABLE_SEED;
        case MSG_REQUEST_META_DATA:                return FSM_EVENT_REQUEST_META_DATA;
        case MSG_REQUEST_SEEDER_BY_FILEID:         return FSM_EVENT_REQUEST_SEEDER_BY_FILEID;
        case MSG_REQUEST_CREATE_SEEDER:            return FSM_EVENT_REQUEST_CREATE_SEEDER;
        case MSG_REQUEST_DELETE_SEEDER:            return FSM_EVENT_REQUEST_DELETE_SEEDER;
        case MSG_REQUEST_CREATE_NEW_SEED:          return FSM_EVENT_REQUEST_CREATE_NEW_SEED;
        case MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID:return FSM_EVENT_REQUEST_PARTICIPATE_SEED_BY_FILEID;
        case MSG_REQUEST_UNPARTICIPATE_SEED:       return FSM_EVENT_REQUEST_UNPARTICIPATE_SEED;
        case MSG_ACK_CREATE_NEW_SEED:              return FSM_EVENT_ACK_CREATE_NEW_SEED;
        case MSG_ACK_PARTICIPATE_SEED_BY_FILEID:   return FSM_EVENT_ACK_PARTICIPATE_SEED_BY_FILEID;
        case MSG_ACK_SEEDER_BY_FILEID:             return FSM_EVENT_ACK_SEEDER_BY_FILEID;
        case MSG_RESPOND_ERROR:                    return FSM_EVENT_RESPOND_ERROR;
        default:                                   return FSM_EVENT_NULL;
    }
}

void tracker_fsm_handler(){
    switch (ctx->current_state)
    {
    case Tracker_FSM_INIT:
        tracker_init();
        break;
    case Tracker_FSM_LISTENING_PEER:
        tracker_listening_peer();
        break;
    case Tracker_FSM_LISTENING_EVENT:
        tracker_listening_event();
        break;
    case Tracker_FSM_HANDLE_EVENT: {
        FSM_TRACKER_EVENT event = map_msg_type_to_fsm_event(header->type);
        printf("FSM_TRACKER_EVENT: %d\n", event);
        tracker_event_handler(event);
        break;
    }
    case Tracker_FSM_ERROR:
        tracker_error_handler();
        break;
    case Tracker_FSM_CLEANUP:
        tracker_cleanup();
        break;
    case Tracker_FSM_CLOSING:
        tracker_closing();
        break;
    default:
        break;
        
    }
}

void tracker_error_handler() {
    printf("FSM Reached error ");
    ctx->current_state = Tracker_FSM_CLOSING;
}

void tracker_closing() {
    printf("FSM Reached closing");
    if (ctx->listen_socket >= 0) {
        close(ctx->listen_socket);
    }
    if (ctx->client_socket >= 0) {
        close(ctx->client_socket);
    }
    ctx->current_state = Tracker_FSM_CLEANUP;

    exit_success();
}

void tracker_cleanup() {
    free(ctx);
    free(header);
    free(body);
    printf("FSM Reached cleanup");
    return;
}

void tracker_init(){
    // Allocate memory for the global structures
    header = malloc(sizeof(TrackerMessageHeader));
    body = malloc(sizeof(TrackerMessageBody));
    
    if (header) {
        memset(header, 0, sizeof(TrackerMessageHeader));
    }
    if (body) {
        memset(body, 0, sizeof(TrackerMessageBody));
    }
    
    init_seeders();
    listen_socketfd = setup_server();
    ctx->listen_socket = listen_socketfd;
    ctx->current_state = Tracker_FSM_LISTENING_PEER;
}

void tracker_close_peer(){
    if (ctx->client_socket >= 0) {
        printf("closing client socket");
        close(ctx->client_socket);
    }
    ctx->current_state = Tracker_FSM_LISTENING_PEER;
}

/* Main function */
int main(void)
{
    ctx = malloc(sizeof(TrackerContext));
    if (ctx) {
        memset(ctx, 0, sizeof(TrackerContext));
    }
    
    tracker_init();

    while (ctx->current_state != Tracker_FSM_CLOSING) {
        tracker_fsm_handler();
    }

    if (ctx->current_state == Tracker_FSM_CLEANUP) {
        tracker_closing();
    }
    return 0;
}

