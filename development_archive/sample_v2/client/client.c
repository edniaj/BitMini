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

void send_file(int sockfd, const char *file_path)
{

    size_t byte_read;
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen(file_path, "rb"); /* read binary */

    if (fp == NULL)
    {
        perror("ERROR opening file for reading");
        return;
    }

    /*fread(dest, sizeof(datatype), chunk size, fp)*/
    byte_read = fread(buffer, 1, BUFFER_SIZE, fp);
    /* fread returns the number of bytes returned */
    while (byte_read > 0)
    {
        /* Handle error */
        if (write(sockfd, buffer, byte_read) < 0)
        {
            perror("ERROR writing to socket");
            fclose(fp);
            return;
        }
        byte_read = fread(buffer, 1, BUFFER_SIZE, fp);
    }

    printf("File transfer complete.\n");
    fclose(fp);
}

void write_metadata(){
    return ;
}

void read_metadata(){
    return ;
}

int main()
{
    char file_path[256];

    /* Ask the user for the file path. */
    printf("Enter the path of the file to send: ");
    scanf("%s",file_path);
    /* meta file is assumed to be in the same directory as the filepath*/


    return EXIT_SUCCESS;
}
