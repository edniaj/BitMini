#ifndef PEER_COMMUNICATION_H
#define PEER_COMMUNICATION_H

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <openssl/sha.h> // for create_chunkHash()

#define CHUNK_DATA_SIZE 1024

// Forward declarations
typedef enum PeerMessageType PeerMessageType;
typedef struct PeerMessageHeader PeerMessageHeader;
typedef struct PeerMessage PeerMessage;
typedef struct ChunkRequest ChunkRequest;
typedef struct TransferChunk TransferChunk;
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

typedef struct BitfieldData
{
    uint8_t data[8192]; // Example max size
} BitfieldData;

// Now define the union that uses those structures
typedef union PeerMessageBody
{
    ChunkRequest chunkRequest;
    TransferChunk transferChunk;
    BitfieldData bitfieldData;
    ssize_t fileID;
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

void create_chunkHash(TransferChunk *chunk)
{
    SHA256_CTX sha256;
    SHA256_Init(&sha256); // Initialize SHA-256 context

    SHA256_Update(&sha256, chunk->chunkData, chunk->totalByte); // Update hash with chunk
    SHA256_Final(chunk->chunkHash, &sha256);
}

int has_chunk(uint8_t *bitfield, ssize_t index)
{
    // Step 1: Calculate which byte in the array contains our bit
    size_t byte_position = index / 8;

    // Step 2: Calculate which bit within that byte we need to check
    int bit_position = index % 8;

    // Step 3: Create a mask with only the target bit set to 1
    uint8_t bit_mask = 1 << bit_position;

    // Step 4: Apply the mask to the byte using bitwise AND
    uint8_t result = bitfield[byte_position] & bit_mask;

    // Step 5: Check if the result is non-zero (meaning the bit was set)
    return result != 0;
}

#endif // PEER_COMMUNICATION_H