#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define DEBUG 1

#define NUM_CHUNK 20

#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#include<errno.h>

#include <sys/ioctl.h>


#define ERR_EXIT(m) \
    do { \
        perror(m); \
        exit(EXIT_FAILURE); \
    } while (0)


void print_current_time_with_us (void);
void error(const char *msg);
const char *get_filename_ext(const char *filename);
void udp_subchunk_size(int file_length, int num_chunk, int *subchunk_size, int *num_subchunk);

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

void print_current_time_with_us (void) {
    long            us; // Microseconds
    time_t          s;  // Seconds
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);

    s  = spec.tv_sec;
    us = round(spec.tv_nsec / 1.0e3); // Convert nanoseconds to microseconds
    if (us > 999999) {
        s++;
        us = 0;
    }

    printf(" %"PRIdMAX".%03ld \n", (intmax_t)s, us);
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

void udp_subchunk_size(int file_length, int num_chunk, int *subchunk_size, int *num_subchunk) {
    // the last to arguments are return values
    
    // sendto() in udp_send() cannot afford too large size of data,
    // (file_length / NUM_CHUNK) bytes may be too large to send,
    // if it is, adjust to ((file_length / NUM_CHUNK) / num_subchunk) bytes for each sent chunk

    *subchunk_size = 0;
    *num_subchunk = 1;
    for (*num_subchunk = 1; (file_length / (num_chunk * *num_subchunk) > 10000); ++*num_subchunk) {
        if (DEBUG) printf("*num_subchunk = %d\n", *num_subchunk);
        if (DEBUG) printf("file_length / (num_chunk * *num_subchunk = %d\n", file_length / (num_chunk * *num_subchunk));
    }
    *subchunk_size = file_length / (num_chunk * *num_subchunk);
    if (DEBUG) printf("*subchunk_size = %d\n", *subchunk_size);
}

int tcp_send(int argc, char *argv[]) {

    int sockfd, clisockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n;

    int full_chunk_size;
    char file_ext[10]; // file extension of the input file
    int write_size; // the third argument sent to write()
    char *chunk_buffer; // save the file chunk to write to client later
    char confirm_buffer[256];
    FILE *file;

    file = fopen(argv[5], "r");
    if (!file) error("ERROR : opening file.\n");
    
    // determine file file length, full_chunk_size, then malloc chunk_buffer 
    size_t pos = ftell(file);    // Current position
    fseek(file, 0, SEEK_END);    // Go to end
    size_t file_length = ftell(file); // read the position which is the size
    fseek(file, pos, SEEK_SET);  // restore original position
    printf("file length = %zu\n", file_length);
    full_chunk_size = file_length / NUM_CHUNK;
    if (DEBUG) printf("full_chunk_size = %d\n", full_chunk_size);
    // both of (file_length) and NUM_CHUNK are int, so:
    //      1. (file_length) is equal to (full_chunk_size * NUM_CHUNK + offset)
    //      2. there may be (NUM_CHUNK + 1) chunks if the offset isn't 0
    //      3. the last chunk has the size (offset)
    //      4. malloc chunk_buffer with (full_chunk_size) which is >= (offset)
    chunk_buffer = (char *)malloc(full_chunk_size);


    //////// create socket and bind. Listen to and accept client

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
    clisockfd = accept(sockfd,
                (struct sockaddr *) &cli_addr,
                &clilen); // clisockfd is the file descriptor for the accepted socket.
    if (clisockfd < 0)
        error("ERROR on accept");
    
    //////// write & read

    // send file extension to client
    bzero(file_ext, sizeof(file_ext));
    strcpy(file_ext, get_filename_ext(argv[5]));  
    n = write(clisockfd, &file_ext, sizeof(file_ext));
    if (n < 0) error("ERROR writing to socket");

    while (1) {
        // except boundary conditions, 
        // the workflow of this while loop :
        // file read --> write chunk size --> write chunk --> read confirm msg

        // fread a file chunk to chunk_buffer
        bzero(chunk_buffer, full_chunk_size);
        int bytes_read = fread(chunk_buffer, 1, full_chunk_size, file);

        if (bytes_read == 0){ // boundary condition (last)
            // all file contents are read, tell clinet that transfering is finished.
            write_size = htonl(22);
            n = write(clisockfd, &write_size, sizeof(write_size));
            if (n < 0) error("ERROR writing to socket"); 
            n = write(clisockfd, "file transfer finished", 22);
            if (n < 0) error("ERROR writing to socket"); 
            printf("Sender: File transfer finished.\n");
            break;
        }

        // write a file chunk to client (sending the chunk size first)
        write_size = htonl(bytes_read);
        n = write(clisockfd, &write_size, sizeof(write_size));
        if (n < 0) error("ERROR writing to socket"); 
        n = write(clisockfd, chunk_buffer, bytes_read);
        if (n < 0) error("ERROR writing to socket");    

        // read confirm message from client
        bzero(confirm_buffer,255);
        n = read(clisockfd,confirm_buffer,255);
        if (n < 0) error("ERROR reading from socket");
    }

    close(clisockfd);
    close(sockfd);

    return 0;

}

int tcp_recv(int argc, char *argv[]){
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    int read_size;
    char *chunk_buffer;
    int first_chunk_flag = 1;
    int full_chunk_size;
    char file_ext[10];
    char file_name[50];
    FILE *file;

    float percent = 0;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    ////// create socket and connect to server

    // create socket
    portno = atoi(argv[4]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    // get server's host name
    server = gethostbyname(argv[3]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    // connect to server
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");


    ////// recv file extension and open receiver.X

    // recv file extension
    bzero(file_ext,sizeof(file_ext));
    n = read(sockfd, &file_ext, sizeof(file_ext));
    if (n < 0) error("ERROR reading from socket 1");
    strcpy(file_name, "receiver.");
    strcat(file_name, file_ext);

    // open receiver.X
    file = fopen(file_name,"w");
    if (file == NULL){
        printf("Unable to create file.\n");
        exit(EXIT_FAILURE);
    }

    while (1){
        if (DEBUG) printf("in tcp_recv() while-------\n");
        // except boundary conditions, 
        // the workflow of this while loop :
        // "read chunk size" --> "read chunk" --> "file write" --> "write confirm msg"

        // read the chunk size
        bzero(&read_size, sizeof(read_size));
        n = read(sockfd, &read_size, sizeof(read_size));
        if (n < 0) error("ERROR reading from socket 2");
        read_size = ntohl(read_size);
        if (DEBUG) printf("read size = %d\n", read_size);

        if (first_chunk_flag) { // boundary condidiotn (initial)
            // the first chunk sent from server has the size (full_chunk_size) mentioned in tcp_send()
            // which is >= (offset, the last chunk size) mentioned in tcp_send()
            // so we malloc chunk_buffer with the size of the first chunk
            full_chunk_size = read_size;
            chunk_buffer = (char *)malloc(full_chunk_size);
            first_chunk_flag = 0;
        }

        
        // check amount of data available for sockfd
        int count;
        do {
            ioctl(sockfd, FIONREAD, &count);
        } while (count < read_size);

        // read a chunk   
        bzero(chunk_buffer, full_chunk_size);
        //printf("strlen(chunk_buffer) = %d\n", strlen(chunk_buffer));
        //memset(chunk_buffer, '\0', full_chunk_size);
        n = read(sockfd, chunk_buffer, read_size);
        if (n < 0) error("ERROR reading from socket 3");

        if (strcmp("file transfer finished", chunk_buffer)) {
            // transfering haven't finished, 

            // fwrite chunk_buffer with read_size byte to receiver.X
            // (the main reason of reading the size of the following chunk every time is to
            // fwrite with the correct size, otherwise, img file may be damaged)
            int s = fwrite(chunk_buffer, 1, full_chunk_size, file);
            if (DEBUG) printf("s = %d\n", s);

            // show percentage and sys time(in microsecond)
            percent += 100.0 / NUM_CHUNK;
            // we will show 100% when the NUM_CHUNK chunks has written to receiver.X,
            // while the last chunk with (offset) bytes hasn't arrived.
            if (percent <= 100) {
                printf("%.0f%% %d-%d-%d %d:%d:%d\t", percent, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                print_current_time_with_us();
            }
        } else { // boundary condition (last)
            // transfer finished.
            printf("Receiver: file transfer finished\n");
            fclose(file);
            break;
        }

        // confirm message: tell server a file chunk has been received.
        n = write(sockfd,"I got your message",18);
        if (n < 0) error("ERROR writing to socket");
    }

    close(sockfd);
    
    return 0;
}

int udp_send(int argc, char *argv[]){
    
    int sockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr;
    struct sockaddr_in peeraddr;
    socklen_t peerlen;
    int n;

    int full_chunk_size;  // the size of chunk which is sent
    char recvbuf[1024] = {0};  // receiving the first call from client
    char file_ext[10];
    int write_size;
    char *chunk_buffer;
    char confirm_buffer[256];
    FILE *file;

    file = fopen(argv[5], "r");
    if (!file) error("ERROR : opening file.\n");
    
    // determine file length, full_chunk_size, then malloc chunk_buffer 
    size_t pos = ftell(file);    // Current position
    fseek(file, 0, SEEK_END);    // Go to end
    size_t file_length = ftell(file); // read the position which is the size
    fseek(file, pos, SEEK_SET);  // restore original position
    printf("file length = %zu\n", file_length);
    int num_subchunk = 0;
    udp_subchunk_size(file_length, NUM_CHUNK, &full_chunk_size, &num_subchunk);
    chunk_buffer = (char *)malloc(full_chunk_size);


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

    // send num_subchunk
    n = sendto(sockfd, &num_subchunk, sizeof(num_subchunk), 0, (struct sockaddr *)&peeraddr, peerlen);
    if (n < 0) error("ERROR : sendto()"); 
    
    while (1) {
        // except boundary conditions, 
        // the workflow of this while loop :
        // file read --> write chunk size --> write chunk --> read confirm msg

        // fread a file chunk to chunk_buffer
        if (DEBUG) printf("ftell(file) = %zu\n", ftell(file));
        bzero(chunk_buffer, full_chunk_size);
        int bytes_read = fread(chunk_buffer, 1, full_chunk_size, file);
        if (DEBUG) printf("bytes_read = %d\n", bytes_read);

        // boundary condition (last)
        if (bytes_read == 0){
            // all file contents are read, tell clinet that transfering is finished.
            write_size = 22;
            n = sendto(sockfd, &write_size, sizeof(write_size), 0, (struct sockaddr *)&peeraddr, peerlen);
            if (n < 0) error("ERROR : sendto()");   
            n = sendto(sockfd, "file transfer finished", 22, 0, (struct sockaddr *)&peeraddr, peerlen);
            if (n < 0) error("ERROR : sendto()");   
            
            printf("Sender: File transfer finished.\n");
            break;
        }

        // write a file chunk to client (sending the chunk size first)
        write_size = bytes_read;
        n = sendto(sockfd, &write_size, sizeof(write_size), 0, (struct sockaddr *)&peeraddr, peerlen);
        if (n < 0) error("ERROR : sendto() 1");   
        n = sendto(sockfd, chunk_buffer, bytes_read, 0, (struct sockaddr *)&peeraddr, peerlen);  
        if (n < 0) error("ERROR : sendto() 2");   

        // read confirm message from client (check if it's needed to resent)
        bzero(confirm_buffer,255);
        n = recvfrom(sockfd, confirm_buffer, sizeof(confirm_buffer), 0, (struct sockaddr *)&peeraddr, &peerlen);
        if (n == -1 && errno != EINTR) ERR_EXIT("recvfrom error");
        if (!strcmp("resent the chunk size and chunk", confirm_buffer)) {
            if (DEBUG) printf("Server: got the resent request============\n");
            fseek(file, ftell(file) - bytes_read, SEEK_SET);
        } else if (!strcmp("I got your message", confirm_buffer)) {
            if (DEBUG) printf("Server: client got the message\n");
        } else {
            printf("invalid confirm message sent from client\n");
            exit(1);
        }
    }

    close(sockfd);

    return 0;

}

int udp_recv(int argc, char *argv[]){
    
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char sendbuf[1024] = {0};
    char file_name[50];
    char file_ext[10];
    int read_size;
    char *chunk_buffer;
    int first_chunk_flag = 1;
    int full_chunk_size;

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

    // call the server
    if (DEBUG) printf("call the server\n");
    n = sendto(sockfd, sendbuf, strlen(sendbuf), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if (n < 0) error("ERROR : sendto()");   

    // recv file extension
    bzero(file_ext,sizeof(file_ext));
    n = recvfrom(sockfd, &file_ext, sizeof(file_ext), 0, NULL, NULL);
    if (n == -1 && errno != EINTR) ERR_EXIT("recvfrom");
    strcpy(file_name, "receiver.");
    strcat(file_name, file_ext);

    // open receiver.X
    file = fopen(file_name,"w");
    if (file == NULL){
        printf("Unable to create file.\n");
        exit(EXIT_FAILURE);
    }

    // recv num_subchunk
    int num_subchunk = 0;
    int tmp_num_subchunk = 0;
    n = recvfrom(sockfd, &num_subchunk, sizeof(num_subchunk), 0, NULL, NULL);
    if (n == -1 && errno != EINTR) ERR_EXIT("recvfrom");

    while (1){
        // except boundary conditions, 
        // the workflow of this while loop :
        // "read chunk size" --> "read chunk" --> "file write" --> "write confirm msg"

        if (DEBUG) printf("in udp_recv() while --------\n");

        if (DEBUG) {
            int test_count = 0;
            ioctl(sockfd, FIONREAD, &test_count);
            printf("test_count = %d\n", test_count);
        }

        // read the chunk size
        n = recvfrom(sockfd, &read_size, sizeof(read_size), 0, NULL, NULL);
        if (n == -1 && errno != EINTR) ERR_EXIT("recvfrom");
        if (DEBUG) printf("in udp_recv() after recv read_size\n");
        if (DEBUG) printf("read_size = %d\n", read_size);

        // boundary condition (initial)
        // determine size of chunk_buffer
        if (first_chunk_flag) {
            full_chunk_size = read_size;
            chunk_buffer = (char *)malloc(full_chunk_size);
            first_chunk_flag = 0;
        }

        // check if the chunk sent by server is lost
        int count;
        int read_chunk_and_file_write = 1;
        ioctl(sockfd, FIONREAD, &count);
        if (count < read_size) {
            // the chunk may loss,
            // however, the chunk may just havn't totally arrive,
            // so sleep for 1 milliseconds and check again.
            usleep(1000);
            if (DEBUG) printf("usleep(100) usleep(100) usleep(100) usleep(100) usleep(100) \n");

            ioctl(sockfd, FIONREAD, &count);
            if (count < read_size) { // double check
                // assert that  the chunk is lost
                // go to "write confirm msg" to tell server to resend
                read_chunk_and_file_write = 0;
                if (DEBUG) printf("need resend. need resend. need resend. need resend. need resend. need resend. \n");
            }
        }
    
        if (read_chunk_and_file_write) {
            // read a chunk
            bzero(chunk_buffer, full_chunk_size);
            n = recvfrom(sockfd, chunk_buffer, read_size, 0, NULL, NULL);
            if (n == -1 && errno != EINTR) ERR_EXIT("recvfrom");
            if (DEBUG) printf("in udp_recv() after recv content(i.e. chunk_buffer)\n");

            if (strcmp("file transfer finished", chunk_buffer)) {
                // transfering haven't finished, 

                // fwrite chunk_buffer with read_size byte to receiver.X
                fwrite(chunk_buffer, 1, read_size, file);

                // show percentage and sys time(in microsecond)
                ++tmp_num_subchunk;
                if (tmp_num_subchunk == num_subchunk) {
                    percent += 100.0 / NUM_CHUNK;
                    if (percent <= 100) {
                        printf("%.0f%% %d-%d-%d %d:%d:%d\t", percent, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                        print_current_time_with_us();
                    }
                    tmp_num_subchunk = 0;
                }
                
            } else { // boundary condition (last)
                // transfering finished
                printf("Receiver: file transfer finished\n");
                fclose(file);
                break;
            }
        }

        // confirm message: tell server a file chunk has been received.
        if (read_chunk_and_file_write) {
            n = sendto(sockfd, "I got your message",18, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            if (n < 0) error("ERROR : sendto()");   
        } else {
            n = sendto(sockfd, "resent the chunk size and chunk",31, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            if (n < 0) error("ERROR : sendto()");   
        }
    }

    close(sockfd);
    
    return 0;
}