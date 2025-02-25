#ifndef CONNECTION_H
#define CONNECTION_H

/*
 * State Machine of Connection:
 * - CONNECTION_DISCONNECTED: No active connection.
 * - CONNECTION_CONNECTING: Establishing a connection.
 * - CONNECTION_HANDSHAKE: Exchanging initial protocol info.
 * - CONNECTION_EXCHANGE_INFO: Exchanging metadata/bitfields.
 * - CONNECTION_DATA_TRANSFER: Actively sending/receiving chunks.
 * - CONNECTION_CLOSING: Gracefully shutting down.
 * - CONNECTION_ERROR: Connection has encountered an error -> Handle Error , either Close or try again.
 * 
 */
typedef enum {
    CONNECTION_DISCONNECTED,
    CONNECTION_CONNECTING,
    CONNECTION_HANDSHAKE,
    CONNECTION_EXCHANGE_INFO,
    CONNECTION_DATA_TRANSFER,
    CONNECTION_CLOSING,
    CONNECTION_ERROR
} ConnectionState;



/*
 * A struct representing a Connection between two peers (or peer-tracker).
 */
typedef struct {
    ConnectionState state;  // Current state of this connection
} Connection;

#endif /* CONNECTION_H */
