#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <sys/file.h>
#include <dirent.h>
#include <sys/wait.h>

void logOperation(const char *message) {
    FILE *logFile = fopen("log.txt", "a");
    if (logFile == NULL) {
        perror("Error opening log file");
        return;
    }

    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    timestamp[strlen(timestamp) - 1] = '\0'; // Remove newline character

    fprintf(logFile, "[%s] %s\n", timestamp, message);
    fclose(logFile);
}

void createDir(const char *folderName) {
    struct stat st;
    if (stat(folderName, &st) == 0) {
        printf("Error: Directory \"%s\" already exists.\n", folderName);
        logOperation("Error: Directory already exists.");
        return;
    }

    if (mkdir(folderName, 0755) == -1) {
        perror("Error creating directory");
        logOperation("Error: Failed to create directory.");
        return;
    }

    printf("Directory \"%s\" created successfully.\n", folderName);
    char logMessage[256];
    snprintf(logMessage, sizeof(logMessage), "Directory \"%s\" created successfully.", folderName);
    logOperation(logMessage);
}

void createFile(const char *fileName) {
    struct stat st;
    if (stat(fileName, &st) == 0) {
        printf("Error: File \"%s\" already exists.\n", fileName);
        logOperation("Error: File already exists.");
        return;
    }

    int fd = open(fileName, O_WRONLY | O_CREAT, 0644);
    if (fd == -1) {
        perror("Error creating file");
        logOperation("Error: Failed to create file.");
        return;
    }

    time_t now = time(NULL);
    char *timestamp = ctime(&now);

    if (write(fd, timestamp, strlen(timestamp)) == -1) {
        perror("Error writing to file");
        close(fd);
        logOperation("Error: Failed to write to file.");
        return;
    }

    close(fd);
    printf("File \"%s\" created successfully with creation timestamp.\n", fileName);
    char logMessage[256];
    snprintf(logMessage, sizeof(logMessage), "File \"%s\" created successfully.", fileName);
    logOperation(logMessage);
}

void listDir(const char *folderName) {
    struct stat st;
    if (stat(folderName, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("Error: Directory \"%s\" not found.\n", folderName);
        logOperation("Error: Directory not found.");
        return;
    }

    DIR *dir = opendir(folderName);
    if (dir == NULL) {
        perror("Error opening directory");
        logOperation("Error: Failed to open directory.");
        return;
    }

    struct dirent *entry;
    printf("Contents of directory \"%s\":\n", folderName);
    while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name);
    }

    closedir(dir);
    char logMessage[256];
    snprintf(logMessage, sizeof(logMessage), "Listed contents of directory \"%s\".", folderName);
    logOperation(logMessage);
}

void listFilesByExtension(const char *folderName, const char *extension) {
    struct stat st;
    if (stat(folderName, &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("Error: Directory \"%s\" not found.\n", folderName);
        logOperation("Error: Directory not found.");
        return;
    }

    DIR *dir = opendir(folderName);
    if (dir == NULL) {
        perror("Error opening directory");
        logOperation("Error: Failed to open directory.");
        return;
    }

    struct dirent *entry;
    int found = 0;
    printf("Files with extension \"%s\" in directory \"%s\":\n", extension, folderName);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char *dot = strrchr(entry->d_name, '.');
            if (dot && !strcmp(dot, extension)) {
                printf("%s\n", entry->d_name);
                found = 1;
            }
        }
    }

    if (!found) {
        printf("No files with extension \"%s\" found in \"%s\".\n", extension, folderName);
    }

    closedir(dir);
    char logMessage[256];
    snprintf(logMessage, sizeof(logMessage), "Listed files with extension \"%s\" in directory \"%s\".", extension, folderName);
    logOperation(logMessage);
}

void readFile(const char *fileName) {
    struct stat st;
    if (stat(fileName, &st) != 0) {
        printf("Error: File \"%s\" not found.\n", fileName);
        logOperation("Error: File not found.");
        return;
    }

    int fd = open(fileName, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        logOperation("Error: Failed to open file.");
        return;
    }

    char buffer[1024];
    ssize_t bytesRead;
    printf("Contents of file \"%s\":\n", fileName);
    //while (bytesRead = read(fd, buffer, sizeof(buffer))) {
    while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0) {

        if (bytesRead == -1) {
            perror("Error reading file");
            close(fd);
            logOperation("Error: Failed to read file.");
            return;
        }
        write(STDOUT_FILENO, buffer, bytesRead);
    }

    close(fd);
    char logMessage[256];
    snprintf(logMessage, sizeof(logMessage), "Read contents of file \"%s\".", fileName);
    logOperation(logMessage);
}

// void appendToFile(const char *fileName, const char *content) {
//     struct stat st;
//     if (stat(fileName, &st) != 0) {
//         printf("Error: File \"%s\" not found.\n", fileName);
//         logOperation("Error: File not found.");
//         return;
//     }

//     int fd = open(fileName, O_WRONLY | O_APPEND);
//     if (fd == -1) {
//         perror("Error opening file");
//         logOperation("Error: Failed to open file.");
//         return;
//     }

//     if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
//         printf("Error: Cannot write to \"%s\". File is locked or read-only.\n", fileName);
//         close(fd);
//         logOperation("Error: File is locked or read-only.");
//         return;
//     }

//     if (write(fd, content, strlen(content)) == -1) {
//         perror("Error writing to file");
//         flock(fd, LOCK_UN);
//         close(fd);
//         logOperation("Error: Failed to write to file.");
//         return;
//     }

//     flock(fd, LOCK_UN);
//     close(fd);
//     printf("Content appended to file \"%s\" successfully.\n", fileName);
//     char logMessage[256];
//     snprintf(logMessage, sizeof(logMessage), "Content appended to file \"%s\".", fileName);
//     logOperation(logMessage);
// }

void appendToFile(const char *fileName, const char *content) {
    struct stat st;
    if (stat(fileName, &st) != 0) {
        printf("Error: File \"%s\" not found.\n", fileName);
        logOperation("Error: File not found.");
        return;
    }

    int fd = open(fileName, O_WRONLY | O_APPEND);
    if (fd == -1) {
        perror("Error opening file");
        logOperation("Error: Failed to open file.");
        return;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
        printf("Error: Cannot write to \"%s\". File is locked or read-only.\n", fileName);
        close(fd);
        logOperation("Error: File is locked or read-only.");
        return;
    }

    // Convert escape sequences (e.g., \n) in the content
    char processedContent[strlen(content) + 1]; // +1 for null terminator
    int j = 0;
    for (int i = 0; content[i] != '\0'; i++) {
        if (content[i] == '\\' && content[i + 1] == 'n') {
            processedContent[j++] = '\n'; // Replace \n with actual newline
            i++; // Skip the 'n' character
        } else {
            processedContent[j++] = content[i];
        }
    }
    processedContent[j] = '\0'; // Null-terminate the processed content

    // Write the processed content to the file
    if (write(fd, processedContent, strlen(processedContent)) == -1) {
        perror("Error writing to file");
        flock(fd, LOCK_UN);
        close(fd);
        logOperation("Error: Failed to write to file.");
        return;
    }

    flock(fd, LOCK_UN);
    close(fd);
    printf("Content appended to file \"%s\" successfully.\n", fileName);
    char logMessage[256];
    snprintf(logMessage, sizeof(logMessage), "Content appended to file \"%s\".", fileName);
    logOperation(logMessage);
}

void deleteFile(const char *fileName) {
    struct stat st;
    if (stat(fileName, &st) != 0) {
        printf("Error: File \"%s\" not found.\n", fileName);
        logOperation("Error: File not found.");
        return;
    }

    if (unlink(fileName) == -1) {
        perror("Error deleting file");
        logOperation("Error: Failed to delete file.");
        return;
    }

    printf("File \"%s\" deleted successfully.\n", fileName);
    char logMessage[256];
    snprintf(logMessage, sizeof(logMessage), "File \"%s\" deleted successfully.", fileName);
    logOperation(logMessage);
}

void deleteDir(const char *folderName) {
    struct stat st;
    if (stat(folderName, &st) != 0) {
        printf("Error: Directory \"%s\" not found.\n", folderName);
        logOperation("Error: Directory not found.");
        return;
    }

    DIR *dir = opendir(folderName);
    if (dir == NULL) {
        perror("Error opening directory");
        logOperation("Error: Failed to open directory.");
        return;
    }

    struct dirent *entry;
    int isEmpty = 1;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            isEmpty = 0;
            break;
        }
    }

    closedir(dir);

    if (!isEmpty) {
        printf("Error: Directory \"%s\" is not empty.\n", folderName);
        logOperation("Error: Directory is not empty.");
        return;
    }

    if (rmdir(folderName) == -1) {
        perror("Error deleting directory");
        logOperation("Error: Failed to delete directory.");
        return;
    }

    printf("Directory \"%s\" deleted successfully.\n", folderName);
    char logMessage[256];
    snprintf(logMessage, sizeof(logMessage), "Directory \"%s\" deleted successfully.", folderName);
    logOperation(logMessage);
}

void showLogs() {
    FILE *logFile = fopen("log.txt", "r");
    if (logFile == NULL) {
        perror("Error opening log file");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), logFile)) {
        printf("%s", line);
    }

    fclose(logFile);
}

void displayHelp() {
    printf("Usage: fileManager <command> [arguments]\n\n");
    printf("Commands:\n");
    printf("createDir \"folderName\"    - Create a new directory\n");
    printf("createFile \"fileName\"    - Create a new file\n");
    printf("listDir \"folderName\"    - List all files in a directory\n");
    printf("listFilesByExtension \"folderName\" \".txt\" - List files with specific extension\n");
    printf("readFile \"fileName\"    - Read a file's content\n");
    printf("appendToFile \"fileName\" \"new content\" - Append content to a file\n");
    printf("deleteFile \"fileName\"    - Delete a file\n");
    printf("deleteDir \"folderName\"    - Delete an empty directory\n");
    printf("showLogs    - Display logs\n");
    printf("exit    - Exit the program\n");
}

int main() {
    char input[256];
    char command[50];
    char arg1[100];
    char arg2[100];

    while (1) {
        printf("\nfileManager> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break; // Exit on EOF
        }

        // Remove newline character from input
        input[strcspn(input, "\n")] = 0;

        // Parse the input
        int numArgs = sscanf(input, "%s %s %[^\n]", command, arg1, arg2);

        if (strcmp(command, "exit") == 0) {
            printf("Exiting program.\n");
            break;
        } else if (strcmp(command, "createDir") == 0 && numArgs >= 2) {
            createDir(arg1);
        } else if (strcmp(command, "createFile") == 0 && numArgs >= 2) {
            createFile(arg1);
        } else if (strcmp(command, "listDir") == 0 && numArgs >= 2) {
            listDir(arg1);
        } else if (strcmp(command, "listFilesByExtension") == 0 && numArgs >= 3) {
            listFilesByExtension(arg1, arg2);
        } else if (strcmp(command, "readFile") == 0 && numArgs >= 2) {
            readFile(arg1);
        } else if (strcmp(command, "appendToFile") == 0 && numArgs >= 3) {
            appendToFile(arg1, arg2);
        } else if (strcmp(command, "deleteFile") == 0 && numArgs >= 2) {
            pid_t pid = fork();
            if (pid == 0) {
                deleteFile(arg1);
                exit(0);
            } else if (pid > 0) {
                wait(NULL); // Wait for the child process to finish
            } else {
                perror("Error forking process");
            }
        } else if (strcmp(command, "deleteDir") == 0 && numArgs >= 2) {
            pid_t pid = fork();
            if (pid == 0) {
                deleteDir(arg1);
                exit(0);
            } else if (pid > 0) {
                wait(NULL); // Wait for the child process to finish
            } else {
                perror("Error forking process");
            }
        } else if (strcmp(command, "showLogs") == 0) {
            showLogs();
        } else if (strcmp(command, "help") == 0) {
            displayHelp();
        } else {
            printf("Invalid command. Type 'help' for a list of commands.\n");
        }
    }

    return 0;
}