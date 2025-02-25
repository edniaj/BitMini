#include <stdlib.h>
#include <stdint.h>
#include <Connection.h>
#ifndef PEER_H
#define PEER_H
/*
State machine of Peer
 * - PEER_REGISTERING: Handshake with Tracker. The peer is informing the tracker about files it can seed. Tracker will record bandwidth
 * - PEER_ACTIVE: Fully active peer (seeding/leeching).
 * - PEER_CLOSING: Gracefully shutdown
 * - PEER_DISCONNECTED: The peer is offline or no longer participating. Clean up functions

State Machine of Chunk:
 * - CHUNK_NOT_REQUESTED: Chunk has not been requested yet.
 * - CHUNK_REQUESTED: Chunk request sent; awaiting data.
 * - CHUNK_DOWNLOADING: Currently receiving data for this chunk.
 * - CHUNK_DOWNLOADED: Chunk received successfully.
 * - CHUNK_ERROR: An error occurred, like timeout or corruption.
 *
 * File Meta Data is issued by Tracker
 * Files must be registered by Tracker before it is allowed to be seed
 */
typedef enum
{
    REGISTERING,
    ACTIVE,
    CLOSING,
    DISCONNECTED
} PeerState;

typedef enum
{
    CHUNK_NOT_REQUESTED,
    CHUNK_REQUESTED,
    CHUNK_DOWNLOADING,
    CHUNK_DOWNLOADED,
    CHUNK_ERROR
} ChunkState;

typedef struct
{
    int fileNumber;
    char filename[128];
    int totalChunk;
    /* We will use this hash to avoid users registering the same filedata. Also, it can be used to verify the files*/
    uint8_t fileHash[32];
} FileMetadata;

typedef struct
{
    FileMetadata fileMetaData;
    ChunkState *ChunkState;
    bool *bitfield;
} ActiveSeed;
;

typedef struct
{
    PeerState state;    // Current state of this peer
    char ipAddress[64]; // includes port number at the back
    ActiveSeed *activeSeed;
    Connection *connection;
} Peer;

/*
Write the function

register file

*/

#endif /* PEER_H */
