// chatserver.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>

#define MAX_CLIENTS 50
#define MAX_USERNAME 17
#define MAX_ROOMNAME 33
#define MAX_ROOMS 50
#define BUFFER_SIZE 4096
#define MAX_FILE_SIZE 3 * 1024 * 1024
#define MAX_UPLOAD_QUEUE 20  // Increased queue size
#define MAX_CONCURRENT_UPLOADS 5  // Max concurrent uploads
#define ROOM_NAME_LEN 32



typedef enum { STATE_COMMAND, STATE_RECEIVING_FILE } ClientState;

typedef struct {
    int sockfd;
    char username[MAX_USERNAME];
    char room[MAX_ROOMNAME];
    ClientState state;               // <-- NEW
    long remaining_file_bytes;      // <-- NEW
    //FileTransfer* current_file;     // <-- NEW
} Client;


// File transfer struct
typedef struct {
    char sender[MAX_USERNAME];
    char receiver[MAX_USERNAME];
    char filename[256];
    int filesize;
    char* filedata;
    time_t enqueued_time;
} FileTransfer;

Client* clients[MAX_CLIENTS];
int client_count = 0;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t upload_slots;
sem_t processing_slots;  // New semaphore for concurrent processing limit

Client* get_client_by_name(const char* name);
int room_user_count(const char* room_name);
void leave_room(Client* cli);

// Queue
FileTransfer* upload_queue[MAX_UPLOAD_QUEUE];
int upload_front = 0, upload_rear = 0, upload_size = 0;
int active_uploads = 0;  // Track active uploads

void log_event(const char* message) {
    pthread_mutex_lock(&log_mutex);
    FILE* logf = fopen("log.txt", "a");
    if (!logf) {
        perror("Log error");
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    time_t now = time(NULL);
    char* timestr = ctime(&now);
    timestr[strcspn(timestr, "\n")] = 0; // remove newline

    fprintf(logf, "%s - %s\n", timestr, message);
    fclose(logf);

    printf("%s - %s\n", timestr, message);

    pthread_mutex_unlock(&log_mutex);
}

// Signal handler
void sigint_handler(int sig) {
    (void)sig; // Unused 
    log_event("[SHUTDOWN] SIGINT received. Disconnecting all clients.");
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) {
            send(clients[i]->sockfd, "Server shutting down.\n", 23, 0);
            close(clients[i]->sockfd);
            free(clients[i]);
            clients[i] = NULL;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    exit(0);
}

// Username check (alphanumeric, length)
int valid_username(const char* name) {
    if (strlen(name) == 0 || strlen(name) >= MAX_USERNAME) return 0;
    for (int i = 0; name[i]; i++) {
        if (!isalnum(name[i])) return 0;
    }
    return 1;
}

// Room name check
int valid_room(const char* room) {
    if (strlen(room) == 0 || strlen(room) >= MAX_ROOMNAME) return 0;
    for (int i = 0; room[i]; i++) {
        if (!isalnum(room[i])) return 0;
    }
    return 1;
}

void add_client(Client* cl) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = cl;
            client_count++;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int sockfd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->sockfd == sockfd) {
            char logbuf[128];
            snprintf(logbuf, sizeof(logbuf), "[DISCONNECT] %s disconnected.", clients[i]->username);
            log_event(logbuf);
            close(clients[i]->sockfd);
            free(clients[i]);
            clients[i] = NULL;
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast_room(const char* room, const char* message, const char* sender) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && strcmp(clients[i]->room, room) == 0 &&
            strcmp(clients[i]->username, sender) != 0) {
            send(clients[i]->sockfd, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void send_private(const char* target, const char* message, const char* sender) {
    (void)sender;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && strcmp(clients[i]->username, target) == 0) {
            send(clients[i]->sockfd, message, strlen(message), 0);
            pthread_mutex_unlock(&clients_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Calculate estimated wait time based on queue position and active uploads
int calculate_wait_time(int queue_position) {
    // Estimate 3 seconds per file (2s processing + 1s overhead)
    int files_ahead = queue_position + active_uploads - MAX_CONCURRENT_UPLOADS;
    if (files_ahead <= 0) return 0;
    
    // Each slot processes files, so divide by concurrent slots
    return (files_ahead * 3) / MAX_CONCURRENT_UPLOADS;
}

void* handle_client(void* arg) {
    char buffer[BUFFER_SIZE];
    Client* cli = (Client*)arg;
    char username_copy[MAX_USERNAME];
    
    add_client(cli);

    pthread_mutex_lock(&clients_mutex);
    strncpy(username_copy, cli->username, MAX_USERNAME - 1);
    username_copy[MAX_USERNAME - 1] = '\0';
    pthread_mutex_unlock(&clients_mutex);

    char logbuf[512];
    snprintf(logbuf, sizeof(logbuf), "[LOGIN] user '%s' connected", username_copy);
    log_event(logbuf);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recv(cli->sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;

        buffer[strcspn(buffer, "\n")] = 0;

        char* cmd = strtok(buffer, " \n");
        if (strncmp(cmd, "/join", 5) == 0) {
            char* room_name = strtok(NULL, " \n");
            if (room_name) {
                char old_room[ROOM_NAME_LEN];
                pthread_mutex_lock(&clients_mutex);
                strncpy(old_room, cli->room, ROOM_NAME_LEN);

                if (strcmp(old_room, room_name) != 0) {
                    if (strlen(old_room) > 0) {
                        leave_room(cli);
                    }

                    strncpy(cli->room, room_name, ROOM_NAME_LEN);

                    char logbuf[BUFFER_SIZE];
                    snprintf(logbuf, sizeof(logbuf),
                        "[ROOM] user '%s' joined room '%s'", cli->username, cli->room);
                    log_event(logbuf);
                }
                pthread_mutex_unlock(&clients_mutex);

                char msg[BUFFER_SIZE];
                snprintf(msg, sizeof(msg), "[INFO] You joined room '%s'\n", cli->room);
                send(cli->sockfd, msg, strlen(msg), 0);
            } else {
                send(cli->sockfd, "[ERROR] Usage: /join <roomname>\n", 33, 0);
            }
        }

        else if (strncmp(buffer, "/rooms", 6) == 0) {
            pthread_mutex_lock(&clients_mutex);
            char rooms_list[BUFFER_SIZE] = "[ROOMS] Available rooms:\n";
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] && strlen(clients[i]->room) > 0) {
                    strcat(rooms_list, clients[i]->room);
                    strcat(rooms_list, "\n");
                }
            }
            pthread_mutex_unlock(&clients_mutex);
            send(cli->sockfd, rooms_list, strlen(rooms_list), 0);
        }

        else if (strncmp(buffer, "/leave", 6) == 0) {
            pthread_mutex_lock(&clients_mutex);
            char old_room[ROOM_NAME_LEN];
            strncpy(old_room, cli->room, ROOM_NAME_LEN);
            cli->room[0] = '\0';
            pthread_mutex_unlock(&clients_mutex);

            send(cli->sockfd, "[INFO] Left room.\n", 19, 0);

            char logbuf[BUFFER_SIZE];
            snprintf(logbuf, sizeof(logbuf),
                "[ROOM] user '%s' left room '%s'",
                cli->username, old_room);
            log_event(logbuf);
        }

        else if (strncmp(buffer, "/broadcast ", 10) == 0) {
            pthread_mutex_lock(&clients_mutex);
            if (strlen(cli->room) == 0) {
                pthread_mutex_unlock(&clients_mutex);
                send(cli->sockfd, "[ERROR] Not in a room.\n", 24, 0);
                continue;
            }
            
            char room_copy[MAX_ROOMNAME];
            strncpy(room_copy, cli->room, MAX_ROOMNAME - 1);
            room_copy[MAX_ROOMNAME - 1] = '\0';
            pthread_mutex_unlock(&clients_mutex);

            char* msg = buffer + 11;
            char fullmsg[BUFFER_SIZE];
            snprintf(fullmsg, sizeof(fullmsg), "[%s]: %s\n", username_copy, msg);
            broadcast_room(room_copy, fullmsg, username_copy);

            snprintf(logbuf, sizeof(logbuf), "[BROADCAST] %s: %s", username_copy, msg);
            log_event(logbuf);
        }

        else if (strncmp(buffer, "/whisper ", 8) == 0) {
            char* rest = buffer + 9;
            char* target = strtok(rest, " ");
            char* msg = strtok(NULL, "\0");

            if (!target || !msg) {
                send(cli->sockfd, "[ERROR] Usage: /whisper <user> <msg>\n", 39, 0);
                continue;
            }

            char fullmsg[BUFFER_SIZE];
            snprintf(fullmsg, sizeof(fullmsg), "[WHISPER %s]: %s\n", username_copy, msg);
            send_private(target, fullmsg, username_copy);

            snprintf(logbuf, sizeof(logbuf), "[WHISPER] %s -> %s: %s", username_copy, target, msg);
            log_event(logbuf);
        }

        else if (strncmp(buffer, "/sendfile ", 9) == 0) {
            char filename[256];
            long filesize;
            char receiver[MAX_USERNAME];

            if (sscanf(buffer + 10, "%s %ld %s", filename, &filesize, receiver) != 3) {
                send(cli->sockfd, "[ERROR] Usage: /sendfile <file> <size> <receiver>\n", 51, 0);
                continue;
            }

            if (filesize > MAX_FILE_SIZE) {
                send(cli->sockfd, "[ERROR] File too large (max 3MB).\n", 36, 0);
                continue;
            }

            // Check if receiver exists
            if (!get_client_by_name(receiver)) {
                send(cli->sockfd, "[ERROR] Receiver not found or offline.\n", 41, 0);
                continue;
            }

            FileTransfer* new_transfer = malloc(sizeof(FileTransfer));
            if (!new_transfer) {
                send(cli->sockfd, "[ERROR] Server memory allocation failed.\n", 43, 0);
                continue;
            }

            strncpy(new_transfer->sender, username_copy, MAX_USERNAME - 1);
            new_transfer->sender[MAX_USERNAME - 1] = '\0';
            strncpy(new_transfer->receiver, receiver, MAX_USERNAME - 1);
            new_transfer->receiver[MAX_USERNAME - 1] = '\0';
            strncpy(new_transfer->filename, filename, 255);
            new_transfer->filename[255] = '\0';
            new_transfer->filesize = filesize;
            new_transfer->filedata = malloc(filesize);

            if (!new_transfer->filedata) {
                send(cli->sockfd, "[ERROR] File buffer allocation failed.\n", 41, 0);
                free(new_transfer);
                continue;
            }

            FILE* f = fopen("received_file_server_side.txt", "wb");
            if (!f) {
                send(cli->sockfd, "[ERROR] Cannot save file on server.\n", 38, 0);
                free(new_transfer->filedata);
                free(new_transfer);
                continue;
            }

            char filebuf[BUFFER_SIZE];
            long received = 0;
            while (received < filesize) {
                ssize_t n = recv(cli->sockfd, filebuf, sizeof(filebuf), 0);
                if (n <= 0) {
                    break;
                }
                fwrite(filebuf, 1, n, f);
                memcpy(new_transfer->filedata + received, filebuf, n);
                received += n;
            }
            fclose(f);

            if (received < filesize) {
                send(cli->sockfd, "[ERROR] Incomplete file received.\n", 36, 0);
                free(new_transfer->filedata);
                free(new_transfer);
                continue;
            }

            pthread_mutex_lock(&file_queue_mutex);
            if (upload_size < MAX_UPLOAD_QUEUE) {
                upload_queue[upload_rear] = new_transfer;
                upload_rear = (upload_rear + 1) % MAX_UPLOAD_QUEUE;
                upload_size++;
                pthread_mutex_unlock(&file_queue_mutex);
                sem_post(&upload_slots);
                send(cli->sockfd, "[INFO] File sent successfully.\n", 34, 0);
            } else {
                pthread_mutex_unlock(&file_queue_mutex);
                send(cli->sockfd, "[ERROR] Upload queue full.\n", 29, 0);
                free(new_transfer->filedata);
                free(new_transfer);
            }
        }


        else if (strncmp(buffer, "/exit", 5) == 0) {
            break;
        }

        else {
            send(cli->sockfd, "[ERROR] Unknown command.\n", 26, 0);
        }
    }

    snprintf(logbuf, sizeof(logbuf), "[DISCONNECT] %s disconnected.", username_copy);
    log_event(logbuf);

    int sockfd = cli->sockfd;
    remove_client(sockfd);
    
    pthread_detach(pthread_self());
    return NULL;
}

int room_user_count(const char* room_name) {
    int count = 0;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && strcmp(clients[i]->room, room_name) == 0) {
            count++;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return count;
}

Client* get_client_by_name(const char* name) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && strcmp(clients[i]->username, name) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return clients[i];
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

void* handle_file_queue(void* arg) {
    (void)arg;
    while (1) {
        sem_wait(&upload_slots);        // Wait for a file to be available
        sem_wait(&processing_slots);    // Wait for a processing slot

        pthread_mutex_lock(&file_queue_mutex);
        if (upload_size == 0) {
            pthread_mutex_unlock(&file_queue_mutex);
            sem_post(&processing_slots);  // Release the processing slot
            continue;
        }

        FileTransfer* file = upload_queue[upload_front];
        upload_queue[upload_front] = NULL;
        upload_front = (upload_front + 1) % MAX_UPLOAD_QUEUE;
        upload_size--;
        active_uploads++;
        pthread_mutex_unlock(&file_queue_mutex);

        if (!file) {
            pthread_mutex_lock(&file_queue_mutex);
            active_uploads--;
            pthread_mutex_unlock(&file_queue_mutex);
            sem_post(&processing_slots);
            continue;
        }

        time_t now = time(NULL);
        int wait_seconds = (int)difftime(now, file->enqueued_time);

        // Notify sender that processing started
        Client* sender = get_client_by_name(file->sender);
        if (sender) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "[INFO] Processing file '%.50s' started (waited %d seconds).\n",
                file->filename, wait_seconds);
            send(sender->sockfd, msg, strlen(msg), 0);
        }

        // Log processing start
        char start_logbuf[512];
        snprintf(start_logbuf, sizeof(start_logbuf),
            "[FILE-PROCESS] Starting upload of '%s' from %s to %s",
            file->filename, file->sender, file->receiver);
        log_event(start_logbuf);

        // Simulate upload processing time
        sleep(2);

        // Save file with timestamp
        char filepath[512];
        struct tm *t = localtime(&now);
        snprintf(filepath, sizeof(filepath), "received_%04d%02d%02d_%02d%02d%02d_%s",
                t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                t->tm_hour, t->tm_min, t->tm_sec,
                file->filename);

        FILE* fp = fopen(filepath, "wb");
        if (fp) {
            fwrite(file->filedata, 1, file->filesize, fp);
            fclose(fp);
            
            // Notify receiver
            Client* receiver = get_client_by_name(file->receiver);
            if (receiver) {
                char notify[256];
                snprintf(notify, sizeof(notify),
                    "[INFO] File '%.50s' from %s has been uploaded successfully.\n",
                    file->filename, file->sender);
                send(receiver->sockfd, notify, strlen(notify), 0);
            }

            // Notify sender of completion
            if (sender) {
                char complete_msg[256];
                snprintf(complete_msg, sizeof(complete_msg),
                    "[INFO] File '%.50s' uploaded successfully to %s.\n",
                    file->filename, file->receiver);
                send(sender->sockfd, complete_msg, strlen(complete_msg), 0);
            }

            // Log completion
            char logbuf[512];
            snprintf(logbuf, sizeof(logbuf),
                "[FILE] '%.100s' from %.16s to %.16s uploaded successfully as '%.200s'.",
                file->filename, file->sender, file->receiver, filepath);
            log_event(logbuf);
        } else {
            char error_logbuf[512];
            snprintf(error_logbuf, sizeof(error_logbuf),
                "[ERROR] Could not write file '%s' from %s",
                file->filename, file->sender);
            log_event(error_logbuf);
        }

        // Cleanup
        free(file->filedata);
        free(file);

        // Update active uploads count and release processing slot
        pthread_mutex_lock(&file_queue_mutex);
        active_uploads--;
        pthread_mutex_unlock(&file_queue_mutex);
        
        sem_post(&processing_slots);  // Release processing slot for next file
    }

    return NULL;
}

void leave_room(Client* cli) {
    char old_room[ROOM_NAME_LEN];
    strncpy(old_room, cli->room, ROOM_NAME_LEN);
    cli->room[0] = '\0';

    char logbuf[BUFFER_SIZE];
    snprintf(logbuf, sizeof(logbuf),
        "[ROOM] user '%s' left room '%s'", cli->username, old_room);
    log_event(logbuf);
}

int username_exists(const char* name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && strcmp(clients[i]->username, name) == 0)
            return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: ./chatserver <port>\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, sigint_handler);
    
    // Initialize semaphores
    sem_init(&upload_slots, 0, 0);  // Files in queue
    sem_init(&processing_slots, 0, MAX_CONCURRENT_UPLOADS);  // Concurrent processing limit

    // Initialize all client pointers to NULL
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = NULL;
    }

    // Initialize upload queue
    for (int i = 0; i < MAX_UPLOAD_QUEUE; i++) {
        upload_queue[i] = NULL;
    }

    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return EXIT_FAILURE;
    }

    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        return EXIT_FAILURE;
    }

    printf("[SERVER] Listening on port %s...\n", argv[1]);
    //printf("[SERVER] Max concurrent uploads: %d, Queue size: %d\n", 
           //MAX_CONCURRENT_UPLOADS, MAX_UPLOAD_QUEUE);
    log_event("[START] Server started.");

    // Create multiple file processing threads
    pthread_t file_threads[MAX_CONCURRENT_UPLOADS];
    for (int i = 0; i < MAX_CONCURRENT_UPLOADS; i++) {
        pthread_create(&file_threads[i], NULL, handle_file_queue, NULL);
    }

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        char username[MAX_USERNAME] = {0};
        recv(client_sock, username, MAX_USERNAME - 1, 0);
        username[strcspn(username, "\n")] = 0;

        if (!valid_username(username)) {
            send(client_sock, "[ERROR] Invalid username.\n", 27, 0);
            close(client_sock);
            continue;
        }

        pthread_mutex_lock(&clients_mutex);
        if (username_exists(username)) {
            pthread_mutex_unlock(&clients_mutex);
            send(client_sock, "[ERROR] Username already taken.\n", 33, 0);
            close(client_sock);
            continue;
        }
        pthread_mutex_unlock(&clients_mutex);

        Client* cli = (Client*)malloc(sizeof(Client));
        cli->state = STATE_COMMAND;
        cli->remaining_file_bytes = 0;
        //cli->current_file = NULL;
        if (!cli) {
            send(client_sock, "[ERROR] Server memory allocation failed.\n", 43, 0);
            close(client_sock);
            continue;
        }
        
        cli->sockfd = client_sock;
        strncpy(cli->username, username, MAX_USERNAME - 1);
        cli->username[MAX_USERNAME - 1] = '\0';
        cli->room[0] = '\0';
        send(client_sock, "[INFO] Joined successfully.\n", 32, 0);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, cli);
    }

    return EXIT_SUCCESS;
}