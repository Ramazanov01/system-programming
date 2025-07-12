
// chatclient.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define BUFFER_SIZE 4096
#define MAX_USERNAME 17
#define MAX_FILENAME 256
#define MAX_FILE_SIZE 3 * 1024 * 1024 // 3MB

int sockfd;
volatile int running = 1;

// Server'dan gelen mesajları dinleyen thread
void* receive_handler(void* arg) {
    (void)arg;
    char buffer[BUFFER_SIZE];
    while (running) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;

        if (strstr(buffer, "[ERROR]"))
            printf("\033[0;31m%s\033[0m", buffer); // kırmızı
        else if (strstr(buffer, "[INFO]"))
            printf("\033[0;32m%s\033[0m", buffer); // yeşil
        else
            printf("%s", buffer);

        printf(">> ");
        fflush(stdout);

    }
    running = 0;
    return NULL;
}

// Dosya gönderme
void send_file(const char* filename, const char* receiver) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        printf("\033[0;31m[ERROR] Cannot open file '%s'.\033[0m\n", filename);
        return;
    }

    struct stat st;
    if (stat(filename, &st) < 0) {
        printf("\033[0;31m[ERROR] File stat failed.\033[0m\n");
        fclose(fp);
        return;
    }

    if (st.st_size > MAX_FILE_SIZE) {
        printf("\033[0;31m[ERROR] File exceeds 3MB limit.\033[0m\n");
        fclose(fp);
        return;
    }

    // Uzantı kontrolü
    const char* allowed[] = {".txt", ".pdf", ".png", ".jpg"};
    int valid = 0;
    for (int i = 0; i < 4; i++) {
        if (strstr(filename, allowed[i])) {
            valid = 1;
            break;
        }
    }
    if (!valid) {
        printf("\033[0;31m[ERROR] Unsupported file type.\033[0m\n");
        fclose(fp);
        return;
    }

    // Server'a header gönder
    char header[BUFFER_SIZE];
    snprintf(header, sizeof(header), "/sendfile %s %ld %s\n", filename, st.st_size, receiver);
    send(sockfd, header, strlen(header), 0);

    // İçeriği gönder
    char filebuf[BUFFER_SIZE];
    size_t n;
    while ((n = fread(filebuf, 1, sizeof(filebuf), fp)) > 0) {
        send(sockfd, filebuf, n, 0);
    }

    fclose(fp);
    printf("\033[0;32m[INFO] File '%s' sent to %s.\033[0m\n", filename, receiver);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: ./chatclient <server_ip> <port>\n");
        return EXIT_FAILURE;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket error");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect error");
        return EXIT_FAILURE;
    }

    // Kullanıcı adı
    char username[MAX_USERNAME];
    printf("Enter username (max 16 chars, alphanumeric): ");
    fgets(username, MAX_USERNAME, stdin);
    username[strcspn(username, "\n")] = 0;
    send(sockfd, username, strlen(username), 0);

    char response[BUFFER_SIZE] = {0};
    recv(sockfd, response, sizeof(response), 0);

    if (strstr(response, "[ERROR]")) {
        printf("\033[0;31m%s\033[0m", response);
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("\033[0;32m%s\033[0m", response);

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_handler, NULL);

    // Ana giriş döngüsü
    while (running) {
        char input[BUFFER_SIZE];
        printf(">> ");
        fflush(stdout);

        if (!fgets(input, BUFFER_SIZE, stdin)) break;
        input[strcspn(input, "\n")] = 0;

        if (strncmp(input, "/sendfile", 9) == 0) {
            char* filename = strtok(input + 10, " ");
            char* target = strtok(NULL, "\0");

            if (!filename || !target) {
                printf("\033[0;31m[ERROR] Usage: /sendfile <filename> <user>\033[0m\n");
                continue;
            }

            send_file(filename, target);
        }
        else if (strncmp(input, "/exit", 5) == 0) {
            send(sockfd, "/exit\n", 6, 0);
            running = 0;
            break;
        }
        else {
            strcat(input, "\n");
            send(sockfd, input, strlen(input), 0);
        }
    }

    close(sockfd);
    pthread_cancel(recv_thread);
    pthread_join(recv_thread, NULL);
    printf("\n[CLIENT] Connection closed.\n");
    return EXIT_SUCCESS;
}
