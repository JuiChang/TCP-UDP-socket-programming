#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define DEBUG 0

#define NUM_CHUNK 20

#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#include<errno.h>

#define ERR_EXIT(m) \
    do { \
        perror(m); \
        exit(EXIT_FAILURE); \
    } while (0)


void print_current_time_with_ms (void);
void error(const char *msg);
const char *get_filename_ext(const char *filename);

int tcp_send(int argc, char *argv[]); // server
int tcp_recv(int argc, char *argv[]); // client
int udp_send(int argc, char *argv[]); // server
int udp_recv(int argc, char *argv[]); // client

int main(int argc, char *argv[]) {
    if (!strcmp(argv[1], "tcp") && !strcmp(argv[2], "send")){
        tcp_send(argc, argv);
    } else if (!strcmp(argv[1], "tcp") && !strcmp(argv[2], "recv")){
        tcp_recv(argc, argv);
    } else if (!strcmp(argv[1], "udp") && !strcmp(argv[2], "send")){
        udp_send(argc, argv);
    } else if (!strcmp(argv[1], "udp") && !strcmp(argv[2], "recv")){
        udp_recv(argc, argv);
    } else
        printf("ERROR : invalid arguments\n");
    return 0;
}

void print_current_time_with_ms (void) {
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

void error(const char *msg) {
    perror(msg);
    exit(1);
}

const char *get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

int tcp_send(int argc, char *argv[]) {

    int sockfd, newsockfd, portno;
    socklen_t clilen;
    char confirm_buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int n;

    int chunk_size;
    int write_size;
    char *buffer_ptr;
    char file_ext[10];
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

    // send file extension
    bzero(file_ext, sizeof(file_ext));
    strcpy(file_ext, get_filename_ext(argv[5]));  
    n = write(newsockfd, &file_ext, sizeof(file_ext));
    if (n < 0) error("ERROR writing to socket");

    while (1) {
        bzero(buffer_ptr, chunk_size);
        int bytes_read = fread(buffer_ptr, 1, chunk_size, file);

        if (bytes_read == 0){
            write_size = 22;
            n = write(newsockfd, &write_size, sizeof(write_size));
            if (n < 0) error("ERROR writing to socket"); 
            n = write(newsockfd, "file transfer finished", 22);
            if (n < 0) error("ERROR writing to socket"); 
            printf("Sender: File transfer finished.\n");
            break;
        }
        //printf("--- %d \n", bytes_read);

        write_size = bytes_read;
        n = write(newsockfd, &write_size, sizeof(write_size));
        if (n < 0) error("ERROR writing to socket"); 
        n = write(newsockfd, buffer_ptr, bytes_read);
        if (n < 0) error("ERROR writing to socket");    

        bzero(confirm_buffer,255);
        n = read(newsockfd,confirm_buffer,255);
        if (n < 0) error("ERROR reading from socket");
    }

    close(newsockfd);
    close(sockfd);

    return 0;

}

int tcp_recv(int argc, char *argv[]){
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    int read_size;
    char buffer[100000]; // may not enough
    char file_ext[10];
    char file_name[50];
    FILE *file;

    float percent = 0;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

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

    // recv file extension
    bzero(file_ext,sizeof(file_ext));
    n = read(sockfd, &file_ext, sizeof(file_ext));
    if (n < 0) error("ERROR reading from socket");
    strcpy(file_name, "receiver.");
    strcat(file_name, file_ext);

    file = fopen(file_name,"w");
    if (file == NULL){
        printf("Unable to create file.\n");
        exit(EXIT_FAILURE);
    }

    while (1){

        n = read(sockfd, &read_size, sizeof(read_size));
        if (n < 0) error("ERROR reading from socket");
        bzero(buffer,99999);
        n = read(sockfd, buffer, 99999);
        if (n < 0) error("ERROR reading from socket");
        //printf("%s\n",buffer);
        if (strcmp("file transfer finished", buffer)) {
            //printf("%lu\n", strlen(buffer));
            //fputs(buffer, file);
            fwrite(buffer, 1, read_size, file);
            //printf("buffer : %lu\n", sizeof(buffer));
            percent += 100.0 / NUM_CHUNK;
            if (percent <= 100) {
                printf("%.0f%% %d-%d-%d %d:%d:%d\t", percent, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                print_current_time_with_ms();
            }
        } else {
            printf("tcp_recv() loop break\n");
            fclose(file);
            break;
        }
        n = write(sockfd,"I got your message",18);
        if (n < 0) error("ERROR writing to socket");
    }

    close(sockfd);
    
    return 0;
}

int udp_send(int argc, char *argv[]){
    printf("udp_send()\n");
    int sockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr;
    struct sockaddr_in peeraddr;
    socklen_t peerlen;
    int n;

    int chunk_size;
    char recvbuf[1024] = {0};
    char file_ext[10];
    int write_size;
    char *buffer_ptr;
    char confirm_buffer[256];
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

    sockfd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = atoi(argv[4]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    
    //////// write & read

    // recv call from client
    peerlen = sizeof(peeraddr);
    memset(recvbuf, 0, sizeof(recvbuf));
    n = recvfrom(sockfd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *)&peeraddr, &peerlen);
    if (n == -1 && errno != EINTR)
        ERR_EXIT("recvfrom error");

    // send file extension
    bzero(file_ext, sizeof(file_ext));
    strcpy(file_ext, get_filename_ext(argv[5]));
    n = sendto(sockfd, &file_ext, sizeof(file_ext), 0, (struct sockaddr *)&peeraddr, peerlen);
    if (n < 0) error("ERROR : sendto()");   
    
    while (1) {
        bzero(buffer_ptr, chunk_size);
        int bytes_read = fread(buffer_ptr, 1, chunk_size, file);

        if (bytes_read == 0){
            write_size = 22;
            n = sendto(sockfd, &write_size, sizeof(write_size), 0, (struct sockaddr *)&peeraddr, peerlen);
            if (n < 0) error("ERROR : sendto()");   
            n = sendto(sockfd, "file transfer finished", 22, 0, (struct sockaddr *)&peeraddr, peerlen);
            if (n < 0) error("ERROR : sendto()");   

            printf("Sender: File transfer finished.\n");
            break;
        }
        //printf("--- %d \n", bytes_read);

        write_size = bytes_read;
        n = sendto(sockfd, &write_size, sizeof(write_size), 0, (struct sockaddr *)&peeraddr, peerlen);
        if (n < 0) error("ERROR : sendto()");   
        n = sendto(sockfd, buffer_ptr, bytes_read, 0, (struct sockaddr *)&peeraddr, peerlen);  
        if (n < 0) error("ERROR : sendto()");   

        bzero(confirm_buffer,255);
        n = recvfrom(sockfd, confirm_buffer, sizeof(confirm_buffer), 0, (struct sockaddr *)&peeraddr, &peerlen);
        if (n == -1 && errno != EINTR)
            ERR_EXIT("recvfrom error");    
    }

    // close(newsockfd);
    close(sockfd);

    return 0;

}

int udp_recv(int argc, char *argv[]){
    printf("udp_recv()\n");
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char sendbuf[1024] = {0};
    char file_name[50];
    char file_ext[10];
    int read_size;
    char buffer[100000]; // may not enough

    float percent = 0;
    FILE *file;

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    portno = atoi(argv[4]);
    sockfd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    server = gethostbyname(argv[3]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(portno);
    bcopy((char *)server->h_addr, 
         (char *)&servaddr.sin_addr.s_addr,
         server->h_length);

    ////// write and read

    // call the server
    if (DEBUG) printf("call the server\n");
    n = sendto(sockfd, sendbuf, strlen(sendbuf), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if (n < 0) error("ERROR : sendto()");   

    // recv the file extension
    bzero(file_ext,sizeof(file_ext));
    n = recvfrom(sockfd, &file_ext, sizeof(file_ext), 0, NULL, NULL);
    if (n == -1 && errno != EINTR) ERR_EXIT("recvfrom");
    strcpy(file_name, "receiver.");
    strcat(file_name, file_ext);

    file = fopen(file_name,"w");
    if (file == NULL){
        printf("Unable to create file.\n");
        exit(EXIT_FAILURE);
    }

    while (1){
        if (DEBUG) printf("in udp_recv() while\n");
        n = recvfrom(sockfd, &read_size, sizeof(read_size), 0, NULL, NULL);
        if (n == -1 && errno != EINTR) ERR_EXIT("recvfrom");
        if (DEBUG) printf("in udp_recv() after recv read_size\n");
        bzero(buffer,99999);
        n = recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (n == -1 && errno != EINTR) ERR_EXIT("recvfrom");
        if (DEBUG) printf("in udp_recv() after recv content(i.e. buffer)\n");
        if (strcmp("file transfer finished", buffer)) {
            fwrite(buffer, 1, read_size, file);
            percent += 100.0 / NUM_CHUNK;
            if (percent <= 100) {
                printf("%.0f%% %d-%d-%d %d:%d:%d\t", percent, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                print_current_time_with_ms();
            }
        } else {
            printf("tcp_recv() loop break\n");
            fclose(file);
            break;
        }
        n = sendto(sockfd, "I got your message",18, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        if (n < 0) error("ERROR : sendto()");   
    }

    close(sockfd);
    
    return 0;
}