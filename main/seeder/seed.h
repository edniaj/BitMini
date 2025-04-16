#ifndef SEED_H
#define SEED_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <dirent.h>
#include "peerCommunication.h"
#include "meta.h"
#include "bitfield.h"

#define STORAGE_DIR "./storage_downloads/"

int setup_seeder_socket(int port);
int handle_peer_request(int client_socketfd);
int handle_peer_connection(int listen_fd);
int send_chunk(int sockfd, FILE *data_file_fp, ssize_t fileID, ssize_t chunkIndex);

char *find_binary_file_path(ssize_t fileID);
char *find_bitfield_file_path(ssize_t fileID);
char *find_metadata_file_path(ssize_t fileID);

#endif /* SEED_H */