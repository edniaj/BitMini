#ifndef LEECH_H
#define LEECH_H



#include <stdint.h>
#include "peerCommunication.h"

uint8_t *request_bitfield(int sockfd, ssize_t fileID);
int request_chunk(int sockfd, ssize_t fileID, ssize_t chunkIndex, TransferChunk *outChunk);
void leech_from_seeder(PeerInfo seeder, char *bitfield_filepath, char *binary_filepath, ssize_t totalChunk, ssize_t fileID);
int leeching(PeerInfo *seeder_list, size_t num_seeders, char *metadata_filepath, char *bitfield_filepath, char *binary_filepath);

int write_chunk_to_file(const char *binary_filepath, const TransferChunk *chunk);
int update_bitfield(const char *bitfield_filepath, ssize_t chunkIndex);
void print_bitfield(const uint8_t *bitfield, size_t bitfield_size, const char *label);


#endif