#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h> // Add for waitpid

#define MAX_CLIENTS 20
#define MAX_PATH_LEN 100
#define BUFFER_SIZE 1024

// Structure to store client information
typedef struct {
    char bank_id[20];
    char operation[20];
    int amount;
    char client_fifo[MAX_PATH_LEN];
    pid_t pid;
} ClientInfo;

// Global variables
ClientInfo clients[MAX_CLIENTS];
int client_count = 0;
int running = 1;
char server_fifo[MAX_PATH_LEN];

// Function prototypes
void signal_handler(int signum);
void read_client_file(const char* filename);
void create_client_processes();
void handle_client(int client_idx);
void cleanup();

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ClientFile> <ServerFIFO_Name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Store server FIFO name
    strncpy(server_fifo, argv[2], sizeof(server_fifo));
    
    // Read client file
    printf("Reading %s..\n", argv[1]);
    read_client_file(argv[1]);
    
    printf("%d clients to connect.. creating clients..\n", client_count);
    
    // Check if server FIFO exists
    struct stat st;
    if (stat(server_fifo, &st) == -1) {
        printf("Cannot connect %s...\n", server_fifo);
        printf("exiting..\n");
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to Adabank..\n");
    
    // Create client processes
    create_client_processes();
    
    // Wait for all child processes to complete
    for (int i = 0; i < client_count; i++) {
        if (clients[i].pid > 0) {
            int status;
            waitpid(clients[i].pid, &status, 0);
        }
    }
    
    printf("exiting..\n");
    return 0;
}

void signal_handler(int signum) {
    running = 0;
    cleanup();
    exit(EXIT_SUCCESS);
}

void read_client_file(const char* filename) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), fp) && client_count < MAX_CLIENTS) {
        // Remove newline character
        line[strcspn(line, "\n")] = '\0';
        
        // Parse client information
        if (sscanf(line, "%s %s %d", 
                  clients[client_count].bank_id, 
                  clients[client_count].operation, 
                  &clients[client_count].amount) == 3) {
            
            // Generate a unique client FIFO name with full path
            snprintf(clients[client_count].client_fifo, sizeof(clients[client_count].client_fifo),
                    "./Client%02d", client_count + 1);
            
            client_count++;
        }
    }
    
    fclose(fp);
}

void create_client_processes() {
    // First create all FIFOs
    for (int i = 0; i < client_count; i++) {
        // Remove FIFO if it already exists
        unlink(clients[i].client_fifo);
        
        // Create client FIFO
        if (mkfifo(clients[i].client_fifo, 0666) == -1) {
            perror("mkfifo");
            exit(EXIT_FAILURE);
        }
        
        // Set proper permissions
        chmod(clients[i].client_fifo, 0666);
    }
    
    // Notify server about the clients
    int server_fd = open(server_fifo, O_WRONLY);
    if (server_fd == -1) {
        perror("open server_fifo");
        exit(EXIT_FAILURE);
    }
    
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "%d %d", getpid(), client_count);
    for (int i = 0; i < client_count; i++) {
        strcat(buffer, " ");
        strcat(buffer, clients[i].client_fifo);
    }
    
    write(server_fd, buffer, strlen(buffer));
    
    // Wait a bit for server to process the message
    usleep(500000); // 500ms delay
    
    close(server_fd);
    
    // Fork processes for each client
    for (int i = 0; i < client_count; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            continue;
        } else if (pid == 0) {
            // Child process
            handle_client(i);
            exit(EXIT_SUCCESS);
        } else {
            clients[i].pid = pid;
        }
    }
}

void handle_client(int client_idx) {
    // Print client connection message
    printf("Client%02d connected..%s %d credits\n", 
           client_idx + 1, 
           clients[client_idx].operation, 
           clients[client_idx].amount);
    
    // Open client FIFO for read/write access
    int fd = open(clients[client_idx].client_fifo, O_RDWR);
    if (fd == -1) {
        perror("open client_fifo");
        return;
    }
    
    // Send request to teller
    char request[BUFFER_SIZE];
    sprintf(request, "%s %s %d", 
            clients[client_idx].bank_id, 
            clients[client_idx].operation, 
            clients[client_idx].amount);
    
    write(fd, request, strlen(request));
    
    // Read response from teller
    char response[BUFFER_SIZE];
    int n = read(fd, response, BUFFER_SIZE);
    if (n > 0) {
        response[n] = '\0';
        printf("%s", response);
    }
    
    close(fd);
}

void cleanup() {
    // Remove all client FIFOs
    for (int i = 0; i < client_count; i++) {
        unlink(clients[i].client_fifo);
    }
}