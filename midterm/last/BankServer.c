#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_CLIENTS 100
#define MAX_PATH_LEN 100
#define BUFFER_SIZE 1024

// Bank account structure
typedef struct {
    char bank_id[20];
    int balance;
    int is_active;
} Account;

// Global variables
Account accounts[MAX_CLIENTS];
int account_count = 0;
int running = 1;
char server_fifo[MAX_PATH_LEN];
char bank_name[50];
char log_file[MAX_PATH_LEN];
int active_tellers[MAX_CLIENTS];
int active_teller_count = 0;

// Function prototypes
void initialize_bank();
void load_bank_log();
void update_bank_log();
void handle_client(int client_pid, char* client_fifo);
void signal_handler(int signum);
void cleanup();
int create_account();
int find_account(char* bank_id);
int deposit(int account_idx, int amount);
int withdraw(int account_idx, int amount);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <BankName> <ServerFIFO_Name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize bank information
    strncpy(bank_name, argv[1], sizeof(bank_name));
    strncpy(server_fifo, argv[2], sizeof(server_fifo));
    snprintf(log_file, sizeof(log_file), "%s.bankLog", bank_name);
    
    printf("%s is active....\n", bank_name);
    
    // Initialize the bank
    initialize_bank();
    
    // Remove server FIFO if it already exists
    unlink(server_fifo);
    
    // Create the server FIFO
    if (mkfifo(server_fifo, 0666) == -1) {
        perror("mkfifo");
        exit(EXIT_FAILURE);
    }
    
    // Set proper permissions
    chmod(server_fifo, 0666);
    
    // Main server loop
    while (running) {
        int server_fd;
        char buffer[BUFFER_SIZE];
        
        printf("Waiting for clients @%s...\n", server_fifo);
        
        // Open the server FIFO for reading
        server_fd = open(server_fifo, O_RDONLY);
        if (server_fd == -1) {
            perror("open server_fifo");
            break;
        }
        
        // Read client information
        int n = read(server_fd, buffer, BUFFER_SIZE);
        if (n <= 0) {
            close(server_fd);
            continue;
        }
        buffer[n] = '\0';
        
        // Parse client information
        char *token = strtok(buffer, " ");
        int client_pid = atoi(token);
        
        token = strtok(NULL, " ");
        int client_count = atoi(token);
        
        printf("- Received %d clients from PID%d\n", client_count, client_pid);
        
        // Process each client
        for (int i = 0; i < client_count; i++) {
            token = strtok(NULL, " ");
            if (token == NULL) break;
            
            char client_fifo[MAX_PATH_LEN];
            strncpy(client_fifo, token, sizeof(client_fifo));
            
            // Create a teller process for this client
            pid_t teller_pid = fork();
            
            if (teller_pid == -1) {
                perror("fork");
                continue;
            } else if (teller_pid == 0) {
                // This is the teller process
                close(server_fd);
                handle_client(client_pid, client_fifo);
                exit(EXIT_SUCCESS);
            } else {
                // This is the parent process
                // Add the teller to the active tellers list
                active_tellers[active_teller_count++] = teller_pid;
                printf("-- Teller %d is active serving %s...\n", teller_pid, client_fifo);
            }
        }
        
        close(server_fd);
        
        // Non-blocking wait for any terminated teller processes
        int status;
        pid_t terminated_pid;
        while ((terminated_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            // Remove the terminated teller from active_tellers
            for (int i = 0; i < active_teller_count; i++) {
                if (active_tellers[i] == terminated_pid) {
                    active_tellers[i] = active_tellers[--active_teller_count];
                    break;
                }
            }
        }
    }
    
    cleanup();
    return 0;
}

void initialize_bank() {
    // Try to load the bank log
    FILE *fp = fopen(log_file, "r");
    if (fp == NULL) {
        printf("No previous logs.. Creating the bank database\n");
    } else {
        printf("Loading bank database from log file\n");
        fclose(fp);
        load_bank_log();
    }
}

void load_bank_log() {
    FILE *fp = fopen(log_file, "r");
    if (fp == NULL) return;
    
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), fp)) {
        // Skip comment lines
        if (line[0] == '#') continue;
        
        // Parse the account information
        char bank_id[20];
        int balance;
        
        if (sscanf(line, "%s %*s %*d %*s %*d %d", bank_id, &balance) == 2 ||
            sscanf(line, "%s %*s %*d %d", bank_id, &balance) == 2) {
            // Add the account to our list
            strncpy(accounts[account_count].bank_id, bank_id, sizeof(accounts[account_count].bank_id));
            accounts[account_count].balance = balance;
            accounts[account_count].is_active = 1;
            account_count++;
        }
    }
    
    fclose(fp);
}

void update_bank_log() {
    FILE *fp = fopen(log_file, "w");
    if (fp == NULL) {
        perror("fopen");
        return;
    }
    
    // Write the timestamp
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(fp, "# %s Log file updated @%02d:%02d %s %d %d\n", 
            bank_name, t->tm_hour, t->tm_min, 
            (t->tm_mon == 3) ? "April" : "Month", 
            t->tm_mday, 1900 + t->tm_year);
    
    // Write active accounts
    for (int i = 0; i < account_count; i++) {
        if (accounts[i].is_active) {
            fprintf(fp, "%s D 0 %d\n", accounts[i].bank_id, accounts[i].balance);
        }
    }
    
    fprintf(fp, "## end of log.\n");
    fclose(fp);
    printf("Bank log updated\n");
}

void handle_client(int client_pid, char* client_fifo) {
    int fd;
    int retries = 60; // Increase the number of retries
    int delay_ms = 100; // ms to wait between retries
    
    // Try to open the client FIFO, with retries
    while (retries--) {
        fd = open(client_fifo, O_RDWR); // Open for read/write to prevent EOF
        if (fd != -1) {
            break; // Successfully opened
        }
        
        if (errno != ENOENT && errno != ENXIO) {
            // If it's not "file not found" or "no such device or address", break
            perror("open client_fifo unexpected error");
            return;
        }
        
        usleep(delay_ms * 1000); // Wait before retry
    }
    
    if (fd == -1) {
        perror("open client_fifo after retries");
        return;
    }
    
    // Read client request
    char buffer[BUFFER_SIZE];
    int n = read(fd, buffer, BUFFER_SIZE);
    if (n <= 0) {
        close(fd);
        return;
    }
    buffer[n] = '\0';
    
    // Parse the request
    char bank_id[20];
    char operation[20];
    int amount;
    
    sscanf(buffer, "%s %s %d", bank_id, operation, &amount);
    
    // Process the request
    char response[BUFFER_SIZE];
    int account_idx;
    
    if (strcmp(bank_id, "N") == 0) {
        // New account
        if (strcmp(operation, "deposit") == 0) {
            account_idx = create_account();
            if (account_idx != -1) {
                deposit(account_idx, amount);
                snprintf(response, sizeof(response), "Client served.. %s\n", accounts[account_idx].bank_id);
                printf("New client deposited %d credits... updating log\n", amount);
                update_bank_log();
            } else {
                snprintf(response, sizeof(response), "Error creating account\n");
            }
        } else {
            snprintf(response, sizeof(response), "New accounts must start with a deposit\n");
        }
    } else {
        // Existing account
        account_idx = find_account(bank_id);
        if (account_idx != -1) {
            if (strcmp(operation, "deposit") == 0) {
                deposit(account_idx, amount);
                snprintf(response, sizeof(response), "Client served.. %s\n", accounts[account_idx].bank_id);
                printf("Client %s deposited %d credits... updating log\n", bank_id, amount);
                update_bank_log();
            } else if (strcmp(operation, "withdraw") == 0) {
                if (withdraw(account_idx, amount)) {
                    if (accounts[account_idx].balance == 0) {
                        snprintf(response, sizeof(response), "Client served.. account closed\n");
                        printf("Client %s withdraws %d credits... updating log... Bye %s\n", 
                               bank_id, amount, bank_id);
                        accounts[account_idx].is_active = 0;
                    } else {
                        snprintf(response, sizeof(response), "Client served.. %s\n", accounts[account_idx].bank_id);
                        printf("Client %s withdraws %d credits... updating log\n", bank_id, amount);
                    }
                    update_bank_log();
                } else {
                    snprintf(response, sizeof(response), "Insufficient funds\n");
                    printf("Client %s withdraws %d credit.. operation not permitted.\n", bank_id, amount);
                }
            } else {
                snprintf(response, sizeof(response), "Unknown operation\n");
            }
        } else {
            snprintf(response, sizeof(response), "Account not found\n");
        }
    }
    
    // Send the response to the client
    write(fd, response, strlen(response));
    close(fd);
}

void signal_handler(int signum) {
    running = 0;
    printf("\nSignal received closing active Tellers\n");
    
    // Send signals to all active tellers
    for (int i = 0; i < active_teller_count; i++) {
        kill(active_tellers[i], SIGTERM);
    }
    
    // Wait for all tellers to terminate
    for (int i = 0; i < active_teller_count; i++) {
        waitpid(active_tellers[i], NULL, 0);
    }
    
    cleanup();
    exit(EXIT_SUCCESS);
}

void cleanup() {
    printf("Removing ServerFIFO... Updating log file...\n");
    unlink(server_fifo);
    update_bank_log();
    printf("%s says \"Bye\"...\n", bank_name);
}

int create_account() {
    if (account_count >= MAX_CLIENTS) return -1;
    
    // Generate a unique bank ID
    sprintf(accounts[account_count].bank_id, "BankID_%02d", account_count + 1);
    accounts[account_count].balance = 0;
    accounts[account_count].is_active = 1;
    
    return account_count++;
}

int find_account(char* bank_id) {
    for (int i = 0; i < account_count; i++) {
        if (accounts[i].is_active && strcmp(accounts[i].bank_id, bank_id) == 0) {
            return i;
        }
    }
    return -1;
}

int deposit(int account_idx, int amount) {
    if (account_idx < 0 || account_idx >= account_count) return 0;
    
    accounts[account_idx].balance += amount;
    return 1;
}

int withdraw(int account_idx, int amount) {
    if (account_idx < 0 || account_idx >= account_count) return 0;
    if (accounts[account_idx].balance < amount) return 0;
    
    accounts[account_idx].balance -= amount;
    return 1;
}