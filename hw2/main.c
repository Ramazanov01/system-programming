//#define _POSIX_C_SOURCE 199309L 
#define _POSIX_C_SOURCE 200809L  // This must be the FIRST line in the file
#undef _POSIX_C_SOURCE           // Just in case it was defined elsewhere
#define _POSIX_C_SOURCE 200809L  // Set it to a version that includes SA_RESTART

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>


#define FIFO1 "/tmp/fifo1"
#define FIFO2 "/tmp/fifo2"
#define FIFO_PERM (S_IRUSR | S_IWUSR)
#define SLEEP_TIME 1  // Reduced from 10 to 1 second
#define LOG_FILE "/tmp/daemon.log"

void handle_sigchld(int sig);
void handle_sighup(int sig);
void handle_sigterm(int sig);
void handle_sigusr1(int sig);
void createFifo(char *fifoPath);
void become_daemon();
void close_file_descriptors();
void child1();
void child2();

int counter = 0;
int running = 1;
FILE *logfile = NULL;

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("Usage: %s <num1> <num2>\n", argv[0]);
        return 1;
    }

    int num1 = atoi(argv[1]);
    int num2 = atoi(argv[2]);
    printf("Input numbers: %d, %d\n", num1, num2);

    // Delete FIFOs if they exist
    unlink(FIFO1);
    unlink(FIFO2);

    createFifo(FIFO1);
    createFifo(FIFO2);
    printf("FIFOs created\n");

    // Convert process to daemon
    become_daemon();
    
    // Open log file after daemonization
    logfile = fopen(LOG_FILE, "w");
    if (!logfile) {
        syslog(LOG_ERR, "Failed to open log file");
        exit(EXIT_FAILURE);
    }
    
    fprintf(logfile, "Daemon started with PID: %d\n", getpid());
    fprintf(logfile, "Input numbers: %d, %d\n", num1, num2);
    fflush(logfile);
    
    // Set up signal handlers 
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        syslog(LOG_ERR, "sigaction error: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    signal(SIGHUP, handle_sighup);
    signal(SIGTERM, handle_sigterm);
    signal(SIGUSR1, handle_sigusr1);
    
    // Create children
    child1();
    child2();
    
    // Write data to FIFOs before children try to read
    FILE *fifo1 = fopen(FIFO1, "w");
    if (!fifo1) {
        syslog(LOG_ERR, "Error opening FIFO1: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    FILE *fifo2 = fopen(FIFO2, "w");
    if (!fifo2) {
        syslog(LOG_ERR, "Error opening FIFO2: %s", strerror(errno));
        fclose(fifo1);
        exit(EXIT_FAILURE);
    }
    
    // Write data and immediately flush
    fprintf(fifo1, "%d\n%d\n", num1, num2);
    fflush(fifo1);
    
    fprintf(fifo2, "findlarger\n%d\n", num2);
    fflush(fifo2);
    
    fclose(fifo1);
    fclose(fifo2);
    
    // Main loop
    while (running) {
        if (counter >= 2) {
            break;
        }
        sleep(1);
    }

    // Cleanup
    unlink(FIFO1);
    unlink(FIFO2);
    fclose(logfile);
    closelog();

    return 0;
}

void child1()
{
    pid_t c1 = fork();
    if (c1 == 0) {
        // Child1 process
        FILE *fifo1 = fopen(FIFO1, "r");
        if (!fifo1) {
            syslog(LOG_ERR, "Child1: Error opening FIFO1: %s", strerror(errno));
            _exit(EXIT_FAILURE);
        }
        
        FILE *fifo2 = fopen(FIFO2, "w");
        if (!fifo2) {
            syslog(LOG_ERR, "Child1: Error opening FIFO2: %s", strerror(errno));
            fclose(fifo1);
            _exit(EXIT_FAILURE);
        }

        int num1, num2;
        if (fscanf(fifo1, "%d", &num1) != 1 || fscanf(fifo1, "%d", &num2) != 1) {
            syslog(LOG_ERR, "Child1: Failed to read numbers");
            fclose(fifo1);
            fclose(fifo2);
            _exit(EXIT_FAILURE);
        }
        
        int larger = (num1 > num2) ? num1 : num2;
        fprintf(fifo2, "%d\n", larger);
        fflush(fifo2);
        
        fclose(fifo1);
        fclose(fifo2);
        _exit(EXIT_SUCCESS);
    }
}

void child2()
{
    pid_t c2 = fork();
    if (c2 == 0) {
        // Child2 process
        FILE *fifo2 = fopen(FIFO2, "r");
        if (!fifo2) {
            syslog(LOG_ERR, "Child2: Error opening FIFO2: %s", strerror(errno));
            _exit(EXIT_FAILURE);
        }

        char command[20];
        int parentNum, child1Result;
        
        if (fscanf(fifo2, "%19s", command) != 1 ||
            fscanf(fifo2, "%d", &parentNum) != 1 ||
            fscanf(fifo2, "%d", &child1Result) != 1) {
            syslog(LOG_ERR, "Child2: Failed to read data");
            fclose(fifo2);
            _exit(EXIT_FAILURE);
        }
        
        if (strcmp(command, "findlarger") == 0) {
            if (child1Result > parentNum) {
                syslog(LOG_INFO, "The larger number is: %d (from Child1)", child1Result);
            } else if (parentNum > child1Result) {
                syslog(LOG_INFO, "The larger number is: %d (from Parent)", parentNum);
            } else {
                syslog(LOG_INFO, "Both numbers are equal: %d", child1Result);
            }
        } else {
            syslog(LOG_ERR, "Unknown command: %s", command);
        }
        
        fclose(fifo2);
        _exit(EXIT_SUCCESS);
    }
}

// [Rest of the helper functions remain the same as in your original code]