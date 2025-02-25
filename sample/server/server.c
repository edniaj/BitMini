#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 12345
#define BUFFER_SIZE 1024

int main() {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    char buffer[BUFFER_SIZE];

    // Open file for writing in binary mode.
    FILE *fp = fopen("received_cat.png", "wb");
    if (fp == NULL) {
        perror("ERROR opening file for writing");
        exit(EXIT_FAILURE);
    }

    // Create a TCP socket.
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    // Set up the server address.
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces.
    serv_addr.sin_port = htons(PORT);

    // Bind the socket.
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        fclose(fp);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections.
    listen(sockfd, 5);
    printf("Server listening on port %d...\n", PORT);

    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
    if (newsockfd < 0) {
        perror("ERROR on accept");
        fclose(fp);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Client connected. Receiving file...\n");

    // Read data from the socket and write it to the file.
    int n;
    while ((n = read(newsockfd, buffer, BUFFER_SIZE)) > 0) {
        fwrite(buffer, 1, n, fp);
    }
    if (n < 0) {
        perror("ERROR reading from socket");
    } else {
        printf("File transfer complete.\n");
    }

    fclose(fp);
    close(newsockfd);
    close(sockfd);
    return 0;
}
