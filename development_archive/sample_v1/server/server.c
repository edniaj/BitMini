#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // For inet_addr, etc.

#define BUFFER_SIZE 1024 * 1024 // 1 MB buffer
#define SERVER_PORT 5555
#define SERVER_IP "127.0.0.1"

int main()
{
    char buffer[BUFFER_SIZE];
    int listen_socketfd, data_socketfd, byte_read;
    int optval = 1;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_addr_length = sizeof(client_addr);    
    FILE *fp;

    /* 1) Create a TCP socket for new clients to connect */
    listen_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socketfd < 0)
    {
        perror("ERROR opening socket");
        exit(EXIT_FAILURE);
    }

    /* Rebinding for us to develop faster. This just works, dont delete this until we are done with development */
    setsockopt(listen_socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    /* 2) Set up the server address. */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; /* INADDR_ANY MEANS ANY INTERFACE. CLIENT.C uses gethostname() to pass value to .s_addr instead*/
    serv_addr.sin_port = htons(SERVER_PORT);

    /* 3) Bind the socket to the port. */
    if (bind(listen_socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR on binding");
        close(listen_socketfd);
        exit(EXIT_FAILURE);
    }

    /* 4) Listen for incoming connections. listen(fd, backlog) , backlog is the queue size for pending connections */
    if (listen(listen_socketfd, 10) < 0)
    {
        perror("ERROR on listen");
        close(listen_socketfd);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d...\n", SERVER_PORT);

    /* 5) Accept a single client connection (basic example).
        accept() is a blocking function.
     */ 
    data_socketfd = accept(listen_socketfd, (struct sockaddr *)&client_addr, &client_addr_length);
    if (data_socketfd < 0)
    {
        perror("ERROR on accept");
        close(listen_socketfd);
        exit(EXIT_FAILURE);
    }
    printf("Client connected. Receiving file...\n");

    /* 6) Open the file for writing in binary mode. 
        We need to create a blank file first, before we write the binary data into it. 
        We will use the .meta file to write this file
    */
    fp = fopen("received_cat.png", "wb");
    if (fp == NULL)
    {
        perror("ERROR opening file for writing");
        close(data_socketfd);
        close(listen_socketfd);
        exit(EXIT_FAILURE);
    }

    // 7) Read data from the socket until client closes.
    byte_read = read(data_socketfd, buffer, sizeof(buffer));
    while (byte_read > 0)
    {
        fwrite(buffer, 1, byte_read, fp);
        byte_read = read(data_socketfd, buffer, sizeof(buffer)); /* read/write for sockets, fread/fwrite for files*/
    }
    /* Handle the end of the file read function*/
    if (byte_read < 0)
    {
        perror("ERROR reading from socket");
    }
    else
    {
        printf("File transfer complete.\n");
    }

    // 8) Clean up.
    fclose(fp);
    close(data_socketfd);
    close(listen_socketfd);
    return 0;
}
