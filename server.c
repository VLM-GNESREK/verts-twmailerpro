#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include "Headers/common.h" // Gemeine Definitionen
#define LDAP_DEPRECATED 1
#include <ldap.h>

#define LDAP_URI     "ldap://ldap.technikum-wien.at:389"
#define LDAP_BASE_DN "dc=technikum-wien,dc=at"

#define BLACKLIST_FILE "blacklist.txt"
#define BLACKLIST_DURATION 60

int is_ip_blacklisted(const char *ip) {
    FILE *f = fopen(BLACKLIST_FILE, "r");
    if (!f) return 0;

    char file_ip[64];
    long until = 0;
    long now = time(NULL);

    while (fscanf(f, "%63s %ld", file_ip, &until) == 2) {
        if (strcmp(file_ip, ip) == 0) {
            if (now < until) {
                fclose(f);
                return 1;  // IP ist noch gesperrt
            }
        }
    }

    fclose(f);
    return 0;
}

void add_ip_to_blacklist(const char *ip) {
    FILE *f = fopen(BLACKLIST_FILE, "a");
    if (!f) return;

    long until = time(NULL) + BLACKLIST_DURATION;

    fprintf(f, "%s %ld\n", ip, until);
    fclose(f);

    printf("[SERVER] Added IP %s to blacklist for %d seconds.\n",
           ip, BLACKLIST_DURATION);
}

// -=- Hilf-Methoden (File IO / String) -=-

int read_complete_line(int sock, char* buffer, int max_size) // Liest eine Zeile und überprüft, dass sie korrekt verarbeitet wird.
{
    int bytes_read = 0;
    char c;
    
    while (bytes_read < max_size - 1) 
    {
        int n = read(sock, &c, 1);
        if (n <= 0) 
        {
            return -1; // Fehler oder Verbindung geschlossen
        }
        if (c == '\n') break; // Zeilenende
        buffer[bytes_read++] = c;
    }
    buffer[bytes_read] = '\0';
    return bytes_read;
}

int is_username_valid(const char* username)
{
    if(!username || strlen(username) > USER_LEN || strlen(username) == 0) return 0;
    for(int i = 0; username[i]; i++)
    {
        if(!((username[i] >= 'a' && username[i] <= 'z') || 
             (username[i] >= '0' && username[i] <= '9'))) return 0;
    }
    return 1;
}

int create_user_folder(const char* username, const char* mail_dir) 
{
    char folder_path[256];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", mail_dir, username);
    
    struct stat st = {0};
    if (stat(folder_path, &st) == -1) 
    {
        if (mkdir(folder_path, 0700) == -1) 
        {
            return 0; // Fehler
        }
    }
    return 1; // Erfolg
}

int count_user_messages(const char* username, const char* mail_dir) 
{
    char folder_path[256];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", mail_dir, username);
    
    DIR* folder = opendir(folder_path);
    if (!folder) 
    {
        return 0; // Keine Nachrichten
    }
    
    int message_count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(folder)) != NULL) 
    {
        if (strstr(entry->d_name, ".msg")) 
        {
            message_count++;
        }
    }
    
    closedir(folder);
    return message_count;
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

// -=- LDAP Authentifizierung -=-
int ldap_authenticate(const char *username, const char *password)
{
    printf("=== LDAP AUTH ===\n");
    printf("Username: '%s'\n", username ? username : "(null)");
    printf("Password length: %zu\n", password ? strlen(password) : 0);

    if (!username || !password || strlen(username) == 0 || strlen(password) == 0) {
        printf("LDAP: empty username or password\n");
        return 0;
    }

    LDAP *ld = NULL;
    int rc = ldap_initialize(&ld, LDAP_URI);
    printf("LDAP initialize: %s\n", ldap_err2string(rc));
    if (rc != LDAP_SUCCESS || !ld) {
        return 0;
    }

    // LDAP Optionen setzen
    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

    // TLS starten
    rc = ldap_start_tls_s(ld, NULL, NULL);
    printf("LDAP start_tls: %s\n", ldap_err2string(rc));
    if (rc != LDAP_SUCCESS) {
        ldap_unbind_ext_s(ld, NULL, NULL);
        return 0;
    }

    // DN konstruieren 
    char user_dn[256];
    snprintf(user_dn, sizeof(user_dn),
             "uid=%s,ou=people,dc=technikum-wien,dc=at",
             username);

    printf("Binding with DN: %s\n", user_dn);

    // Simple SASL Bind (wie im Unterricht)
    struct berval cred;
    cred.bv_val = (char *)password;
    cred.bv_len = strlen(password);

    rc = ldap_sasl_bind_s(
        ld,
        user_dn,
        LDAP_SASL_SIMPLE,
        &cred,
        NULL,
        NULL,
        NULL
    );

    printf("LDAP bind result: %s\n", ldap_err2string(rc));

    ldap_unbind_ext_s(ld, NULL, NULL);

    return (rc == LDAP_SUCCESS);
}

// -=- Command Handler -=-

int handle_login(int sock, char *out_username)
{
    char ldap_user[LINE_LEN];
    char ldap_pass[LINE_LEN];

    if(read_complete_line(sock, ldap_user, sizeof(ldap_user)) <= 0) return 0;
    if(read_complete_line(sock, ldap_pass, sizeof(ldap_pass)) <= 0) return 0;

    if(!is_username_valid(ldap_user))
    {
        write(sock, RESP_ERR, strlen(RESP_ERR));
        write(sock, "\n", 1);
        return 0;
    }

      if (!ldap_authenticate(ldap_user, ldap_pass)) {
        write(sock, RESP_ERR, strlen(RESP_ERR));
        write(sock, "\n", 1);
        return 0;
    }

    strcpy(out_username, ldap_user);
    write(sock, RESP_OK, strlen(RESP_OK));
    write(sock, "\n", 1);
    return 1;
}

void process_send_command(int client_sock, const char *mail_dir, const char *session_user) 
{
    char receiver[USER_LEN + 2];
    char subject[SUBJECT_LEN + 2];
    char line_buffer[LINE_LEN];

    if(read_complete_line(client_sock, receiver, sizeof(receiver)) <= 0 ||
       read_complete_line(client_sock, subject, sizeof(subject)) <= 0) return;

    FILE* message_file = NULL; 
    int is_valid = 1; // Flag only after connection is established

    // validation
    if (!is_username_valid(session_user) || !is_username_valid(receiver)) 
    {
        printf("Ungültiger Benutzername empfangen. Nachricht wird verworfen.\n");
        is_valid = 0; 
    } 
    else 
    {
        printf("Neue Nachricht: %s -> %s [%s]\n", session_user, receiver, subject);
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
                fprintf(message_file, "Sender: %s\n", session_user);
                fprintf(message_file, "Receiver: %s\n", receiver);
                fprintf(message_file, "Subject: %s\n", subject);
                fprintf(message_file, "\n");
                printf("Speichere Nachricht in: %s\n", file_path);
            }
        }
    }
    
    while (1) 
    {
        if(read_complete_line(client_sock, line_buffer, sizeof(line_buffer)) < 0) return;
        if(strcmp(line_buffer, ".") == 0) break;

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

void process_list_command(int client_sock, const char *mail_dir, const char *session_user) 
{
    printf("Nachrichten auflisten für: %s\n", session_user);
    
    int message_count = 0;
    char **sorted_files = get_sorted_messages(session_user, mail_dir, &message_count);
    
    // Anzahl an Client senden
    char count_buffer[32];
    snprintf(count_buffer, sizeof(count_buffer), "%d\n", message_count);
    write(client_sock, count_buffer, strlen(count_buffer));
    
    printf("Gefunden: %d Nachrichten\n", message_count);
    
    if (message_count > 0 && sorted_files) 
    {
        char folder_path[256];
        snprintf(folder_path, sizeof(folder_path), "%s/%s", mail_dir, session_user);

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

void process_read_command(int client_sock, const char *mail_dir, const char *session_user) 
{
    char msg_number_str[32];
    int msg_number;
    
    if(read_complete_line(client_sock, msg_number_str, sizeof(msg_number_str)) <= 0)
    {
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        return;
    }
    
    msg_number = atoi(msg_number_str);
    printf("Nachricht lesen: User=%s, Nr=%d\n", session_user, msg_number);
    
    int message_count = 0;
    char** sorted_files = get_sorted_messages(session_user, mail_dir, &message_count);
    
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
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", mail_dir, session_user, filename_to_read);
    
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

void process_delete_command(int client_sock, const char *mail_dir, const char *session_user) 
{
    char msg_number_str[32];
    int msg_number;
    
    if(read_complete_line(client_sock, msg_number_str, sizeof(msg_number_str)) <= 0)
    {
        write(client_sock, RESP_ERR, strlen(RESP_ERR));
        write(client_sock, "\n", 1);
        return;
    }
    
    msg_number = atoi(msg_number_str);
    printf("Nachricht löschen: User=%s, Nr=%d\n", session_user, msg_number);

    int message_count = 0;
    char** sorted_files = get_sorted_messages(session_user, mail_dir, &message_count);
    
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
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", mail_dir, session_user, filename_to_delete);
    
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

// -=- Client Handler -=-
void handle_client(int client_socket, const char *mail_dir)
{
    char client_command[32];
    char session_user[USER_LEN + 1] = "";
    int is_logged_in = 0;
    int failed_attempts = 0;   // Zähler pro Verbindung

    //IP-Adresse holen
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr*)&addr, &len);

    char client_ip[32];
    strcpy(client_ip, inet_ntoa(addr.sin_addr));

    printf("[Client %d] Connected from IP: %s\n", getpid(), client_ip);

    //BLACKLIST CHECK
    if (is_ip_blacklisted(client_ip)) {
        printf("[Client %d] IP %s is BLACKLISTED → terminating connection.\n", getpid(), client_ip);
        write(client_socket, "ERR\n", 4);
        close(client_socket);
        exit(0);
    }
    
    while (1)
    {
        if (read_complete_line(client_socket, client_command, sizeof(client_command)) <= 0)
            break;

        printf("[Client %d] Command: %s\n", getpid(), client_command);

        // LOGIN
        if (strcmp(client_command, CMD_LOGIN) == 0)
        {
            // Zu viele Fehlversuche → Verbindung beenden
            if (failed_attempts >= 3)
            {
                 add_ip_to_blacklist(client_ip);
                write(client_socket, RESP_ERR, strlen(RESP_ERR));
                write(client_socket, "\n", 1);
                printf("[Client %d] Too many failed attempts --> BLACKLISTED \n", getpid());
                break;  
            }

            // Login ausführen
            if (handle_login(client_socket, session_user))
            {
                is_logged_in = 1;
                failed_attempts = 0; // Reset bei Erfolg
                printf("[Client %d] User %s logged in.\n", getpid(), session_user);
            }
            else
            {
                failed_attempts++;
                printf("[Client %d] Login failed (%d/3).\n", getpid(), failed_attempts);

                if (failed_attempts >= 3)
                {
                    add_ip_to_blacklist(client_ip);
                    printf("[Client %d] BLACKLISTED: %s\n", getpid(), client_ip);
                    break;
                }
            }
        }

        // QUIT
        else if (strcmp(client_command, CMD_QUIT) == 0)
        {
            break;
        }

        // Alles andere REQUIRES LOGIN
        else if (!is_logged_in)
        {
            write(client_socket, RESP_ERR, strlen(RESP_ERR));
            write(client_socket, "\n", 1);
        }

        // SEND
        else if (strcmp(client_command, CMD_SEND) == 0)
        {
            process_send_command(client_socket, mail_dir, session_user);
        }

        // LIST
        else if (strcmp(client_command, CMD_LIST) == 0)
        {
            process_list_command(client_socket, mail_dir, session_user);
        }

        // READ
        else if (strcmp(client_command, CMD_READ) == 0)
        {
            process_read_command(client_socket, mail_dir, session_user);
        }

        // DELETE
        else if (strcmp(client_command, CMD_DEL) == 0)
        {
            process_delete_command(client_socket, mail_dir, session_user);
        }

        // Unbekannter Command
        else
        {
            write(client_socket, RESP_ERR, strlen(RESP_ERR));
            write(client_socket, "\n", 1);
        }
    }

    close(client_socket);
    exit(0);
}

int main(int argc, char *argv[]) 
{
    
    // Parameter überprüfen

    if (argc != 3) 
    {
        printf("Verwendung: %s <Port> <Mail-Verzeichnis>\n", argv[0]);
        printf("Beispiel: %s 8080 mailspool\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    char* mail_directory = argv[2];
    mkdir(mail_directory, 0700);
    
    signal(SIGCHLD, SIG_IGN);

    // Server-Socket erstellen

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if(bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
    {
        perror("Bind failed.");
        return 1;
    }
    listen(server_socket, 5);
    
    printf("TW-Mailer Pro Server gestartet auf Port %d\n", port);
    printf("Mail-Verzeichnis: %s\n", mail_directory);
    printf("Warte auf Client-Verbindungen...\n");
    
    // Hauptschleife für Client-Verbindungen
    while (1) 
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if(client_socket < 0) continue;

        pid_t pid = fork();

        if(pid < 0)
        {
            perror("Fork failed.");
            close(client_socket);
        }
        else if(pid == 0) // Child
        {
            printf("\n--- Neue Client-Verbindung ---\n");
            close(server_socket);
            handle_client(client_socket, mail_directory);
            printf("--- Client-Verbindung geschlossen ---\n");
        }
        else // Parent
        {
            close(client_socket);
        }
    }
    
    close(server_socket);
    return 0;
}