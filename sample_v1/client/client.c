#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define BUFFER_SIZE 1024 * 1024
#define SERVER_PORT 5555
#define SERVER_IP "127.0.0.1"

int setup_connection()
{

    /*
    socketfd : TCP port
    AF_INET -> IP v4
    AF_INET6 -> IP v6
    SOCK_STREAM -> Helps us with the TCP handshake
    SOCK_RAW -> We implement the TCP handshake ourself, rather than SOCK_STREAM that abstracts the TCP handshake away

    */
    int client_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *server = gethostbyname(SERVER_IP);
    struct sockaddr_in serv_addr;

    if (client_socketfd < 0)
    {
        perror("ERROR opening socket");
        return -1;
    }

    if (server == NULL)
    {
        fprintf(stderr, "ERROR, no such host\n");
        close(client_socketfd);
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(SERVER_PORT); /* Server's port */

    // Connect to the server.
    if (connect(client_socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting");
        close(client_socketfd);
        return -1;
    }

    printf("Connected to server.\n");
    return client_socketfd;
}

void send_file(int sockfd, const char *file_path)
{

    size_t nread;
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen(file_path, "rb"); /* read binary */

    if (fp == NULL)
    {
        perror("ERROR opening file for reading");
        return;
    }

    /*fread(dest, sizeof(datatype), chunk size, fp)*/
    nread = fread(buffer, 1, BUFFER_SIZE, fp);
    /* fread returns the number of bytes returned */
    while (nread > 0)
    {
        /* Handle error */
        if (write(sockfd, buffer, nread) < 0)
        {
            perror("ERROR writing to socket");
            fclose(fp);
            return;
        }
        nread = fread(buffer, 1, BUFFER_SIZE, fp);
    }

    printf("File transfer complete.\n");
    fclose(fp);
}

int main()
{
    char file_path[256];
    int sockfd = setup_connection();
    if (sockfd == -1)
    {
        fprintf(stderr, "Failed to establish connection.\n");
        return EXIT_FAILURE;
    }

    /* Ask the user for the file path. */
    printf("Enter the path of the file to send: ");
    scanf("%s",file_path);

    /* Remove newline character if present.
    Send file to the server. */
    send_file(sockfd, file_path);
    close(sockfd);

    return EXIT_SUCCESS;
}
