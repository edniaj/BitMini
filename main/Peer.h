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
    int fileID;
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
we need functions for

register file in Tracker
get all files available to leech
get seeders available to leech by filename


Leecher function to Tracker
request_seeder_all_file()
request_seeder_by_file(fileID)


Seeder function to Tracker
request_register_file(FileMetaData where fileID set to 0) Tracker will register the file based on the fileHash



Leecher  to Seeder
request_bitfield(fileID)
request_chunk(fileID, chunkIndex)

Seeder to Leecher
respond_bitfield(fileID)
respond_chunk(fileID, chunkIndex)

Tracker to Leecher
respond_seeder_all_file()
respond_seeder_by_file(fileID)

Tracker to seeder
respond_register_file() -> return ack
respond_seeder_all_file() -> return all files' seeders
respond_seeder_by_file(fileID) -> return seed list

*/

#endif /* PEER_H */
