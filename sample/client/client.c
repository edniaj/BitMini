#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define PORT 12345
#define BUFFER_SIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[BUFFER_SIZE];

    // Create a TCP socket.
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(EXIT_FAILURE);
    }

    // Get the server host information (localhost).
    server = gethostbyname("127.0.0.1");
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Set up the server address structure.
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(PORT);

    // Connect to the server.
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server. Sending file...\n");

    // Open the file (cat.png) for reading in binary mode.
    FILE *fp = fopen("./cat.png", "rb");
    if (fp == NULL) {
        perror("ERROR opening file for reading");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Read from file and send its content to the server.
    size_t nread;
    while ((nread = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        if (write(sockfd, buffer, nread) < 0) {
            perror("ERROR writing to socket");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    printf("File transfer complete.\n");

    fclose(fp);
    close(sockfd);
    return 0;
}
