#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "zlib.h"

#define SERVER_PORT (3000)
#define LISTENNQ (5)
#define MAXLINE (100000)
#define MAXTHREAD (20)

struct extension 
{
    char* filetype;
    char* header_content;
}

extension_mapping[] = {
    {"jpg", "image/jpg" },
    {"jpeg","image/jpeg"},
    {"png", "image/png" },
    {"ico", "image/ico" },
    {"txt", "text/plain" },
    {"html", "text/html" },
    {"css", "text/css" },
    {"pdf", "application/pdf"},
    {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    { NULL, NULL }
};

void* request_func(void *args);

void compression(FILE* source, char* output)
{
    z_stream stream;
    char input[MAXLINE];
    
    // initialize the z stream
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);

    // compression start
    stream.avail_in = fread(input, 1, MAXLINE, source);

    // configure the input char array for compression
    stream.next_in = input;
    stream.avail_out = MAXLINE;
    stream.next_out = output;
    deflate(&stream, Z_FINISH);
    // clean up the stream
    (void) deflateEnd(&stream);
}

void decompression(char* input, char* output)
{
    z_stream stream;
    
    // initialize the z stream
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    inflateInit2(&stream, 15);

    // compression start
    stream.avail_in = strlen(input);

    // configure the input char array for compression
    stream.next_in = input;
    stream.avail_out = MAXLINE;
    stream.next_out = output;
    inflate(&stream, Z_NO_FLUSH);
    // clean up the stream
    inflateEnd(&stream);
}

int main(int argc, char **argv)
{
    int listenfd, connfd;
    struct sockaddr_in server_address, client_address;
    socklen_t len = sizeof(struct sockaddr_in);
    char ip_str[INET_ADDRSTRLEN] = {0};
	int threads_count = 0;
	pthread_t threads[MAXTHREAD];

    // setup listening socket of server
    listenfd = socket(AF_INET, SOCK_STREAM, 0); // SOCK_STREAM : TCP
    if (listenfd < 0) 
    {
        printf("Error: seutp listen socket\n");
        return 0;
    }

    // initialize server address
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY; // IP address: 0.0.0.0
    server_address.sin_port = htons(SERVER_PORT); // port #

    // bind the listening socket to the server address
    if (bind(listenfd, (struct sockaddr *)&server_address, sizeof(struct sockaddr)) < 0) 
    {
        printf("Error: bind socket to server address\n");
        return 0;
    }

    if (listen(listenfd, LISTENNQ) < 0) 
    {
        printf("Error: listen\n");
        return 0;
    }

    // keep processing incoming requests
    while (1) 
    {
        // handshaking between client and server
        connfd = accept(listenfd, (struct sockaddr *)&client_address, &len);
        if (connfd < 0) 
        {
            printf("Error: accept\n");
            return 0;
        }

        inet_ntop(AF_INET, &(client_address.sin_addr), ip_str, INET_ADDRSTRLEN);
        printf("Incoming connection from %s : %hu with fd: %d\n", ip_str, ntohs(client_address.sin_port), connfd);

		// create new thread to process the request
		if (pthread_create(&threads[threads_count], NULL, request_func, (void *)connfd) != 0) 
        {
			printf("Error when creating thread %d\n", threads_count);
			return 0;
		}

        // error when no of thread exceed the deafult no
		if (++threads_count >= MAXTHREAD)
			break;
    }

	printf("Max thread number reached, wait for all threads to finish and exit...\n");

    int i;

	for (i = 0; i < MAXTHREAD; ++i)
		pthread_join(threads[i], NULL);

    return 0;
}

void* request_func(void *args)
{
    // get the connection socket
    int connfd = (int)args;
    int rcv_status;
    char* search_ptr;
    char request_file[MAXLINE] = {0};
    char client_request[MAXLINE] = {0};
    char file_content[MAXLINE] = {0};
    char buff[MAXLINE] = {0};
    FILE* file_flag;
   
    // receive client request
    rcv_status = recv(connfd, client_request, sizeof client_request, 0);
    if (rcv_status == -1)
    {
        strcat(buff, "Error: receiving client request message");
        printf("Error: receiving client request message\n");
        write(connfd, buff, strlen(buff));
        close(connfd);
        return;
    }

    // check if it is a HTTP request
    search_ptr = strstr(client_request, "HTTP/");
    if (search_ptr == NULL)
    {
        strcat(buff, "Error: not a HTTP request");
        printf("Error: not a HTTP request\n");
        write(connfd, buff, strlen(buff));
        close(connfd);
        return;
    }
    
    // check if it is using GET method
    if (strncmp(client_request, "GET ", 4) == 0)
        sscanf(client_request, "GET %s HTTP/1.1", search_ptr);
    else
    {
        strcat(buff, "Error: not a valid request method");
	    printf("Error: not a valid request method\n");
        write(connfd, buff, strlen(buff));
        close(connfd);
        return;
    }

    // check if it is requesting homepage
    if(strcmp(search_ptr, "/") == 0)
    	strcat(request_file, "index.html");
    else
    {
	    strcat(request_file, ".");
        strcat(request_file, search_ptr);
    }

    // get the file type from URL check which content type the requested file is
    char* extract = strchr(request_file, '.');
    int j;
    for (j = 0; extension_mapping[j].filetype != NULL; j++)
    {
        if (strstr(extract, extension_mapping[j].filetype) != NULL)
        {
            printf("Supported file type detected. Attempting to open file.\n");

            // check if the file is openable
            file_flag = fopen(request_file, "rb");

            if (file_flag == NULL)
            {
                printf("Error: 404 Not Found\n");
                strcat(buff, "HTTP/1.1 404 Not Found\r\n");
                strcat(buff, "Server: COMP4621PROJECT\r\n");
                strcat(buff, "Content-Type: text/html\r\n");
                strcat(buff, "Transfer-Encoding: chunked\r\n\r\n");
                strcat(buff, "33\r\n");
                strcat(buff, "<html><head><title>404 Not Found</head></title>\r\n");
                strcat(buff, "36\r\n");
                strcat(buff, "<body><p>404 File Not Found!</p></body></html>\r\n\r\n");
		write(connfd, buff, strlen(buff));
            }
            else
            {
                printf("File found. Now going to perform chunked encoding and put it in buffer.\n");
                strcat(buff, "HTTP/1.1 200 OK\r\n");
                strcat(buff, "Server: COMP4621PROJECT\r\n");
                strcat(buff, "Content-Type: ");
                strcat(buff, extension_mapping[j].header_content);
                strcat(buff, "\r\n");

		
		// compression function in beta
                compression(file_flag, file_content);
		send(connfd, file_content, strlen(file_content), 0);
        char output[MAXLINE] = {0};
        decompression(file_content, output);
        printf("%s\n", output);
		

		/*
                // get the file line by line and send in chunks
                if (extension_mapping[j].filetype == "html" && extension_mapping[j].filetype != "css" && extension_mapping[j].filetype != "txt")
		{
		    strcat(buff, "Transfer-Encoding: chunked\r\n\r\n");
		    write(connfd, buff, strlen(buff));
		    
		    while (fgets(file_content, sizeof file_content, file_flag) != NULL)
		    {
			// conversion from decimal to hexa
			char hex[8] = {0};
			int file_length = strlen(file_content);
			sprintf(hex, "%x", file_length);

			// embed the chunk size and corresponding chunk content into the buffer
			send(connfd, hex, strlen(hex), 0);
			send(connfd, "\r\n", 2, 0);
			send(connfd, file_content, file_length, 0);
			send(connfd, "\r\n", 2, 0);
		    }
		    // adding a 0 at the end of response indicates the end of chunks
		    send(connfd, "0\r\n\r\n", 5, 0);
		}
		else
		{
		    strcat(buff, "Transfer-Encoding: chunked\r\n\r\n");
		    write(connfd, buff, strlen(buff));
		    
		    int byte_count = 0;
		    while ((byte_count = fread(file_content, 1, 512, file_flag)) > 0)
		    {
		        // conversion from decimal to hexa
		        char hex[8] = {0};
			sprintf(hex, "%x", byte_count);
			
			// embed the chunk size and corresponding chunk content into the buffer
			send(connfd, hex, strlen(hex), 0);
			send(connfd, "\r\n", 2, 0);
			send(connfd, file_content, byte_count, 0);
			send(connfd, "\r\n", 2, 0);
		    }
		    
		    // adding a 0 at the end of response indicates the end of chunks
		    send(connfd, "0\r\n\r\n", 5, 0);
		}
		*/
            }
            break;
        }
    }

    // close the connection
    printf("Connection to client closed.\n");
    close(connfd);
}
