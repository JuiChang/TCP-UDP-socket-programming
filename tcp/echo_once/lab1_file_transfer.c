#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define NUM_CHUNK 7

#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

void print_current_time_with_ms (void)
{
    long            ms; // Microseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    ms = round(spec.tv_nsec / 1.0e3); // Convert nanoseconds to microseconds
    if (ms > 999999) {
        s++;
        ms = 0;
    }

    printf(" %"PRIdMAX".%03ld \n",
           (intmax_t)s, ms);
}

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int tcp_send(int argc, char *argv[]); // server
int tcp_recv(int argc, char *argv[]); // client
int udp_send(int argc, char *argv[]);
int udp_recv(int argc, char *argv[]);

int main(int argc, char *argv[]){
    if (!strcmp(argv[1], "tcp") && !strcmp(argv[2], "send")){
        printf("main() if : tcp_send\n");
        tcp_send(argc, argv);
    } else if (!strcmp(argv[1], "tcp") && !strcmp(argv[2], "recv")){
        tcp_recv(argc, argv);
    } else if (!strcmp(argv[1], "udp") && !strcmp(argv[2], "send")){
        udp_send(argc, argv);
    } else if (!strcmp(argv[1], "udp") && !strcmp(argv[2], "recv")){
        udp_recv(argc, argv);
    } else
        printf("ERROR : arguments invalid\n");
    return 0;
}

int tcp_send(int argc, char *argv[]){

    int sockfd, newsockfd, portno;
    socklen_t clilen;
    char confirm_buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int n;

    int chunk_size;
    char *buffer_ptr;
    FILE *file;
    size_t nread;

    file = fopen(argv[5], "r");
    if (!file)
        error("ERROR : opening file.\n");
    
    size_t pos = ftell(file);    // Current position
    fseek(file, 0, SEEK_END);    // Go to end
    size_t length = ftell(file); // read the position which is the size
    fseek(file, pos, SEEK_SET);  // restore original position
    printf("length = %zu\n", length);
    chunk_size = length / NUM_CHUNK;
    buffer_ptr = (char *)malloc(chunk_size);


    //////// create socket and connect with client

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = atoi(argv[4]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    
    listen(sockfd,5);
    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd,
                (struct sockaddr *) &cli_addr,
                &clilen); // newsockfd is the file descriptor for the accepted socket.
    if (newsockfd < 0)
        error("ERROR on accept");
    
    //////// write & read
    while (1) {
        bzero(buffer_ptr, chunk_size);
        int bytes_read = fread(buffer_ptr, 1, chunk_size, file);

        if (bytes_read == 0){
            n = write(newsockfd, "file transfer finished", 22);
            if (n < 0) 
                error("ERROR writing to socket"); 
            printf("Sender: File transfer finished.\n");
            break;
        }
        //printf("--- %d \n", bytes_read);

        n = write(newsockfd, buffer_ptr, bytes_read);
        if (n < 0) 
            error("ERROR writing to socket");    

        bzero(confirm_buffer,255);
        n = read(newsockfd,confirm_buffer,255);
        if (n < 0) 
            error("ERROR reading from socket");

        // if ( bytes_read < chunk_size ){
        //     n = write(newsockfd, "file transfer finished", 22);
        //     if (n < 0) 
        //         error("ERROR writing to socket"); 
        //     printf("Sender: File transfer finished.\n");
        //     break;
        // }
    }

    close(newsockfd);
    close(sockfd);

    return 0;

}

int tcp_recv(int argc, char *argv[]){
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[100000]; // may not enough
    int percent = 0;
    FILE *file;

    portno = atoi(argv[4]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    server = gethostbyname(argv[3]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");

    file = fopen("receiver.jpg","w");
    if (file == NULL){
        printf("Unable to create file.\n");
        exit(EXIT_FAILURE);
    }
    
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    while (1){
        bzero(buffer,99999);
        n = read(sockfd, buffer, 99999);
        if (n < 0) 
            error("ERROR reading from socket");
        //printf("%s\n",buffer);
        if (strcmp("file transfer finished", buffer)) {
            printf("%lu\n", strlen(buffer));
            //fputs(buffer, file);
            fwrite(buffer, 1, 9485, file);
            printf("buffer : %lu\n", sizeof(buffer));
            percent += 5;               // yet : should send the offset first
            if (percent != 100) {
                if (percent == 105)
                    percent = 100;
                printf("%d%% %d-%d-%d %d:%d:%d\t", percent, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                print_current_time_with_ms();
            }
        } else {
            printf("tcp_recv() loop break\n");
            fclose(file);
            break;
        }
        n = write(sockfd,"I got your message",18);
        if (n < 0) 
            error("ERROR writing to socket");
    }

    close(sockfd);
    
    return 0;
}

int udp_send(int argc, char *argv[]){
    return 0;
}

int udp_recv(int argc, char *argv[]){
    return 0;
}