#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <dirent.h>

// Gleiche Definitionen wie im Client für Konsistenz
#define USER_LEN 8
#define SUBJECT_LEN 80
#define LINE_LEN 1024

// Befehls-Konstanten
#define CMD_SEND "SEND"
#define CMD_LIST "LIST"
#define CMD_READ "READ" 
#define CMD_DEL "DEL"
#define CMD_QUIT "QUIT"

// Antwort-Konstanten
#define RESP_OK "OK"
#define RESP_ERR "ERR"

int read_complete_line(int sock, char* buffer, int max_size) {
    int bytes_read = 0;
    char c;
    
    while (bytes_read < max_size - 1) {
        if (read(sock, &c, 1) <= 0) {
            return -1; // Fehler oder Verbindung geschlossen
        }
        if (c == '\n') {
            break; // Zeilenende erreicht
        }
        buffer[bytes_read++] = c;
    }
    buffer[bytes_read] = '\0';
    return bytes_read;
}

int create_user_folder(const char* username, const char* mail_dir) {
    char folder_path[256];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", mail_dir, username);
    
    struct stat st = {0};
    if (stat(folder_path, &st) == -1) {
        if (mkdir(folder_path, 0700) == -1) {
            return 0; // Fehler
        }
    }
    return 1; // Erfolg
}

int count_user_messages(const char* username, const char* mail_dir) {
    char folder_path[256];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", mail_dir, username);
    
    DIR* folder = opendir(folder_path);
    if (!folder) {
        return 0; // Keine Nachrichten
    }
    
    int message_count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(folder)) != NULL) {
        if (strstr(entry->d_name, ".msg")) {
            message_count++;
        }
    }
    
    closedir(folder);
    return message_count;
}

int is_username_valid(const char* username)
{
    int len = strlen(username);

    if(len == 0 || len > USER_LEN)
    {
        return 0;
    }

    for(int i = 0; i < len; i++)
    {
        char c = username[i];
        if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
        {
            return 0;
        }
    }
    return 1;
}

int compare_filenames_numerically(const void *a, const void *b)
{
    const char *filename_a = *(const char**)a;
    const char *filename_b = *(const char**)b;

    return atoi(filename_a) - atoi(filename_b);
}

char **get_sorted_messages(const char *username, const char* mail_dir, int *out_msg_count)
{
    *out_msg_count = 0;

    char folder_path[256];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", mail_dir, username);

    DIR *folder = opendir(folder_path);
    if(!folder)
    {
        return NULL;
    }

    int count = 0;
    struct dirent *entry;
    while((entry = readdir(folder)) != NULL)
    {
        if(strstr(entry->d_name, ".msg"))
        {
            count++;
        }
    }

    if(count == 0)
    {
        closedir(folder);
        return NULL;
    }

    char **filenames = malloc(count *sizeof(char*));
    if(!filenames)
    {
        closedir(folder);
        return NULL;
    }

    rewinddir(folder);
    int i = 0;
    while((entry = readdir(folder)) != NULL)
    {
        if(strstr(entry->d_name, ".msg"))
        {
            filenames[i] = strdup(entry->d_name);
            if(!filenames[i])
            {
                for(int j = 0; j < i; j++) free(filenames[j]);
                free(filenames);
                closedir(folder);
                return NULL;
            }
            i++;
        }
    }
    closedir(folder);

    qsort(filenames, count, sizeof(char*), compare_filenames_numerically);

    *out_msg_count = count;
    return filenames;
}

void free_sorted_messages(char** filenames, int count) 
{
    if (filenames) 
    {
        for (int i = 0; i < count; i++) 
        {
            free(filenames[i]); 
        }
        free(filenames); 
    }
}

void process_send_command(int client_sock, const char* mail_dir) 
{
    char sender[USER_LEN + 2];
    char receiver[USER_LEN + 2];
    char subject[SUBJECT_LEN + 2];
    char line_buffer[LINE_LEN];
    FILE* message_file = NULL; 
    int is_valid = 1; // Flag only after connection is established
    
    // Reads all headers
    if (read_complete_line(client_sock, sender, sizeof(sender)) <= 0 ||
        read_complete_line(client_sock, receiver, sizeof(receiver)) <= 0 ||
        read_complete_line(client_sock, subject, sizeof(subject)) <= 0) 
    {
        return; 
    }

    // validation
    if (!is_username_valid(sender) || !is_username_valid(receiver)) 
    {
        printf("Ungültiger Benutzername empfangen. Nachricht wird verworfen.\n");
        is_valid = 0; 
    } 
    else 
    {
        printf("Neue Nachricht: %s -> %s [%s]\n", sender, receiver, subject);
    }
    
    if (is_valid) 
    {
        if (!create_user_folder(receiver, mail_dir)) 
        {
            is_valid = 0; 
        }
        
        if (is_valid) 
        {
            int next_msg_number = count_user_messages(receiver, mail_dir) + 1;
            char file_path[256];
            snprintf(file_path, sizeof(file_path), "%s/%s/%d.msg", mail_dir, receiver, next_msg_number);
            
            message_file = fopen(file_path, "w");
            if (!message_file) 
            {
                is_valid = 0; 
            } 
            else 
            {
                fprintf(message_file, "Sender: %s\n", sender);
                fprintf(message_file, "Receiver: %s\n", receiver);
                fprintf(message_file, "Subject: %s\n", subject);
                fprintf(message_file, "\n");
                printf("Speichere Nachricht in: %s\n", file_path);
            }
        }
    }
    
    while (1) 
    {
        if (read_complete_line(client_sock, line_buffer, sizeof(line_buffer)) < 0) 
        {
            if (message_file) fclose(message_file); 
            return; 
        }
        
        if (strcmp(line_buffer, ".") == 0) 
        {
            break; 
        }
        
        if (is_valid && message_file) 
        {
            fprintf(message_file, "%s\n", line_buffer);
        }
    }
    
    if (is_valid && message_file) 
    {
        fclose(message_file);
        write(client_sock, RESP_OK, strlen(RESP_OK));
        write(client_sock, "\n", 1);
        printf("Nachricht erfolgreich gespeichert.\n");
    } 
    else 
    {
        if (message_file) fclose(message_file);
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        printf("Nachricht wurde verworfen (Fehler oder ungültiger User).\n");
    }
}

void process_list_command(int client_sock, const char* mail_dir) 
{
    char username[USER_LEN + 2];
    
    if (read_complete_line(client_sock, username, sizeof(username)) <= 0) 
    {
        write(client_sock, "0\n", 2);
        return;
    }

    if (!is_username_valid(username)) 
    {
        printf("Ungültiger Benutzername für LIST: %s\n", username);
        write(client_sock, "0\n", 2); 
        return;
    }
    
    printf("Nachrichten auflisten für: %s\n", username);
    
    int message_count = 0;
    char** sorted_files = get_sorted_messages(username, mail_dir, &message_count);
    
    // Anzahl an Client senden
    char count_buffer[32];
    snprintf(count_buffer, sizeof(count_buffer), "%d\n", message_count);
    write(client_sock, count_buffer, strlen(count_buffer));
    
    printf("Gefunden: %d Nachrichten\n", message_count);
    
    if (message_count > 0 && sorted_files) 
    {
        char folder_path[256];
        snprintf(folder_path, sizeof(folder_path), "%s/%s", mail_dir, username);
            
        for (int i = 0; i < message_count; i++) 
        {
            char file_path[512];
            // Den sortierten Dateinamen verwenden
            snprintf(file_path, sizeof(file_path), "%s/%s", folder_path, sorted_files[i]);
            
            FILE* message_file = fopen(file_path, "r");
            if (message_file) 
            {
                char subject_line[SUBJECT_LEN + 50];
                
                fgets(subject_line, sizeof(subject_line), message_file); // Sender
                fgets(subject_line, sizeof(subject_line), message_file); // Receiver
                if (fgets(subject_line, sizeof(subject_line), message_file)) // Subject
                { 
                    if (strncmp(subject_line, "Subject: ", 9) == 0) 
                    {
                        char* pure_subject = subject_line + 9;
                        char* newline_pos = strchr(pure_subject, '\n');
                        if (newline_pos) *newline_pos = '\0';
                        
                        write(client_sock, pure_subject, strlen(pure_subject));
                        write(client_sock, "\n", 1);
                    }
                }
                fclose(message_file);
            }
        }
    }
    
    free_sorted_messages(sorted_files, message_count);
}

/**
 * Verarbeitet den READ Befehl - Liest eine bestimmte Nachricht
 */
void process_read_command(int client_sock, const char* mail_dir) 
{
    char username[USER_LEN + 2];
    char msg_number_str[32];
    int msg_number;
    
    if (read_complete_line(client_sock, username, sizeof(username)) <= 0 ||
        read_complete_line(client_sock, msg_number_str, sizeof(msg_number_str)) <= 0) 
    {
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        return;
    }

    if (!is_username_valid(username)) 
    {
        printf("Ungültiger Benutzername für READ/DEL: %s\n", username);
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        return;
    }
    
    msg_number = atoi(msg_number_str);
    printf("Nachricht lesen: User=%s, Nr=%d\n", username, msg_number);
    
    int message_count = 0;
    char** sorted_files = get_sorted_messages(username, mail_dir, &message_count);
    
    if (msg_number < 1 || msg_number > message_count || !sorted_files) 
    {
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        free_sorted_messages(sorted_files, message_count);
        return;
    }
    
    // Den korrekten Dateinamen aus der sortierten Liste holen
    const char* filename_to_read = sorted_files[msg_number - 1];
    
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", mail_dir, username, filename_to_read);
    
    FILE* message_file = fopen(file_path, "r");
    if (!message_file) 
    {
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        free_sorted_messages(sorted_files, message_count);
        return;
    }
    
    // OK senden und Nachrichteninhalt übertragen
    write(client_sock, RESP_OK, strlen(RESP_OK));
    write(client_sock, "\n", 1);
    
    char file_buffer[LINE_LEN];
    while (fgets(file_buffer, sizeof(file_buffer), message_file)) 
    {
        write(client_sock, file_buffer, strlen(file_buffer));
    }
    
    fclose(message_file);
    write(client_sock, ".\n", 2);
    printf("Nachricht erfolgreich gelesen\n");
    
    free_sorted_messages(sorted_files, message_count);
}

/**
 * Verarbeitet den DEL Befehl - Löscht eine Nachricht
 */
void process_delete_command(int client_sock, const char* mail_dir) 
{
    char username[USER_LEN + 2];
    char msg_number_str[32];
    int msg_number;
    
    if (read_complete_line(client_sock, username, sizeof(username)) <= 0 ||
        read_complete_line(client_sock, msg_number_str, sizeof(msg_number_str)) <= 0) 
    {
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        return;
    }

    if (!is_username_valid(username)) 
    {
        printf("Ungültiger Benutzername für READ/DEL: %s\n", username);
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        return;
    }
    
    msg_number = atoi(msg_number_str);
    printf("Nachricht löschen: User=%s, Nr=%d\n", username, msg_number);

    int message_count = 0;
    char** sorted_files = get_sorted_messages(username, mail_dir, &message_count);
    
    if (msg_number < 1 || msg_number > message_count || !sorted_files) 
    {
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        free_sorted_messages(sorted_files, message_count);
        return;
    }
    
    // Den korrekten Dateinamen aus der sortierten Liste holen
    const char* filename_to_delete = sorted_files[msg_number - 1];
    
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", mail_dir, username, filename_to_delete);
    
    if (remove(file_path) == 0) 
    {
        write(client_sock, RESP_OK, strlen(RESP_OK));
        write(client_sock, "\n", 1);
        printf("Nachricht erfolgreich gelöscht\n");
    } 
    else 
    {
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        printf("Löschen fehlgeschlagen\n");
    }
    
    free_sorted_messages(sorted_files, message_count);
}

int main(int argc, char *argv[]) {
    // Parameter überprüfen
    if (argc != 3) {
        printf("Verwendung: %s <Port> <Mail-Verzeichnis>\n", argv[0]);
        printf("Beispiel: %s 8080 mailspool\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    char* mail_directory = argv[2];
    
    // Mail-Hauptverzeichnis erstellen
    mkdir(mail_directory, 0700);
    
    // Server-Socket erstellen
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;
    
    // Socket binden und auf Verbindungen warten
    bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address));
    listen(server_socket, 5);
    
    printf("TW-Mailer Server gestartet auf Port %d\n", port);
    printf("Mail-Verzeichnis: %s\n", mail_directory);
    printf("Warte auf Client-Verbindungen...\n");
    
    // Hauptschleife für Client-Verbindungen
    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        printf("\n--- Neue Client-Verbindung ---\n");
        
        char client_command[32];
        
        // Befehle vom Client verarbeiten
        while (1) {
            if (read_complete_line(client_socket, client_command, sizeof(client_command)) <= 0) {
                break; // Verbindung beendet
            }
            
            printf("Empfangener Befehl: %s\n", client_command);
            
            if (strcmp(client_command, CMD_SEND) == 0) {
                process_send_command(client_socket, mail_directory);
            } else if (strcmp(client_command, CMD_LIST) == 0) {
                process_list_command(client_socket, mail_directory);
            } else if (strcmp(client_command, CMD_READ) == 0) {
                process_read_command(client_socket, mail_directory);
            } else if (strcmp(client_command, CMD_DEL) == 0) {
                process_delete_command(client_socket, mail_directory);
            } else if (strcmp(client_command, CMD_QUIT) == 0) {
                printf("Client beendet Verbindung\n");
                break;
            } else {
                write(client_socket, RESP_ERR, strlen(RESP_ERR));
                write(client_socket, "\n", 1);
            }
        }
        
        close(client_socket);
        printf("--- Client-Verbindung geschlossen ---\n");
    }
    
    close(server_socket);
    return 0;
}