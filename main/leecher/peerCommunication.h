#ifndef PEER_COMMUNICATION_H
#define PEER_COMMUNICATION_H

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <openssl/sha.h> // for create_chunkHash()

#define CHUNK_DATA_SIZE 1024
#define BITFIELD_SIZE 1024
// Forward declarations
typedef enum PeerMessageType PeerMessageType;
typedef struct PeerMessageHeader PeerMessageHeader;
typedef struct PeerMessage PeerMessage;
typedef struct ChunkRequest ChunkRequest;
typedef struct TransferChunk TransferChunk;
typedef struct BitfieldRequest BitfieldRequest;
typedef struct BitfieldData BitfieldData;
typedef struct PeerInfo PeerInfo;
typedef union PeerMessageBody PeerMessageBody;

// First define the enum since it's used by PeerMessageHeader
/* We only need PeerInfo + minimal message container */
typedef struct PeerInfo
{
    char ip_address[64];
    char port[16];
} PeerInfo;

typedef enum PeerMessageType
{
    MSG_REQUEST_BITFIELD = 1,
    MSG_ACK_REQUEST_BITFIELD,
    MSG_REQUEST_CHUNK,
    MSG_ACK_REQUEST_CHUNK,
    MSG_SEND_CHUNK,
    MSG_ACK_SEND_CHUNK,
} PeerMessageType;

// Define the simple structures first
typedef struct ChunkRequest
{
    ssize_t fileID;
    ssize_t chunkIndex;
} ChunkRequest;

typedef struct TransferChunk
{
    ssize_t fileID;
    ssize_t chunkIndex;
    ssize_t totalByte;
    char chunkData[CHUNK_DATA_SIZE];
    uint8_t chunkHash[32];
} TransferChunk;

typedef struct BitfieldRequest
{
    ssize_t fileID;
} BitfieldRequest;

typedef struct Bitfield{
    uint8_t* bitfield;
} Bitfield;

// Now define the union that uses those structures
typedef union PeerMessageBody
{
    ChunkRequest chunkRequest;
    TransferChunk transferChunk;
    BitfieldRequest bitfieldRequest;
    Bitfield bitfield;
} PeerMessageBody;

// Define the header
typedef struct PeerMessageHeader
{
    PeerMessageType type;
    ssize_t bodySize;
} PeerMessageHeader;

// Finally define the complete message
typedef struct PeerMessage
{
    PeerMessageHeader header;
    PeerMessageBody body;
} PeerMessage;



int has_chunk(uint8_t *bitfield, ssize_t index);

// Function declaration only - remove the implementation
void create_chunkHash(TransferChunk *chunk);



#endif // PEER_COMMUNICATION_H