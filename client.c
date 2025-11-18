#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Maximale Längen für die Eingabe
#define USER_LEN 8
#define SUBJECT_LEN 80
#define LINE_LEN 1024

// Befehle die wir zum Server schicken können
#define CMD_SEND "SEND"
#define CMD_LIST "LIST" 
#define CMD_READ "READ"
#define CMD_DEL "DEL"
#define CMD_QUIT "QUIT"

// Antworten die der Server schickt
#define RESP_OK "OK"
#define RESP_ERR "ERR"
int connect_to_server(const char* server_ip, int port) {
    // Wenn "localhost" angegeben wurde, ersetze durch 127.0.0.1
    if (strcmp(server_ip, "localhost") == 0) {
        server_ip = "127.0.0.1";
    }

    // Socket erstellen
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Error: Socket could not be created\n");
        return -1;
    }
    
    // Server Adresse vorbereiten
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    // IP Adresse umwandeln
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        printf("Error: Invalid server address\n");
        close(sock);
        return -1;
    }
    
    // Verbindung herstellen
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error: Connection to server failed\n");
        close(sock);
        return -1;
    }
    
    return sock;
}

void read_server_line(int sock, char* buffer, int size) {
    int i = 0;
    char c;
    
    while (i < size - 1 && read(sock, &c, 1) > 0) {
        if (c == '\n') break;
        buffer[i++] = c;
    }
    buffer[i] = '\0';
}

void send_message_to_server(int sock) {
    char sender[USER_LEN + 2];
    char receiver[USER_LEN + 2]; 
    char subject[SUBJECT_LEN + 2];
    char line[LINE_LEN];
    
    printf("--- Send Message ---\n");
    
    // Benutzerdaten eingeben
    printf("Your username: ");
    fgets(sender, sizeof(sender), stdin);
    sender[strcspn(sender, "\n")] = '\0';
    
    printf("Receiver username: ");
    fgets(receiver, sizeof(receiver), stdin);
    receiver[strcspn(receiver, "\n")] = '\0';
    
    printf("Subject: ");
    fgets(subject, sizeof(subject), stdin);
    subject[strcspn(subject, "\n")] = '\0';
    
    // Befehl an Server senden
    write(sock, CMD_SEND, strlen(CMD_SEND));
    write(sock, "\n", 1);
    write(sock, sender, strlen(sender));
    write(sock, "\n", 1);
    write(sock, receiver, strlen(receiver));
    write(sock, "\n", 1);
    write(sock, subject, strlen(subject));
    write(sock, "\n", 1);
    
    // Nachrichtentext eingeben
    printf("Enter your message (end with '.' on empty line):\n");
    while (1) {
        fgets(line, sizeof(line), stdin);
        
        // Ende der Nachricht?
        if (strcmp(line, ".\n") == 0) {
            write(sock, ".\n", 2);
            break;
        }
        
        write(sock, line, strlen(line));
    }
    
    // Antwort vom Server lesen
    char response[LINE_LEN];
    read_server_line(sock, response, sizeof(response));
    printf("Server: %s\n", response);
}


void list_user_messages(int sock) {
    char username[USER_LEN + 2];
    
    printf("--- List Messages ---\n");
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';
    
    // Befehl an Server senden
    write(sock, CMD_LIST, strlen(CMD_LIST));
    write(sock, "\n", 1);
    write(sock, username, strlen(username));
    write(sock, "\n", 1);
    
    // Anzahl der Nachrichten lesen
    char count_str[32];
    read_server_line(sock, count_str, sizeof(count_str));
    int count = atoi(count_str);
    
    printf("Found %d messages:\n", count);
    
    // Betreffzeilen lesen
    for (int i = 0; i < count; i++) {
        char subject[SUBJECT_LEN + 1];
        read_server_line(sock, subject, sizeof(subject));
        printf("%d. %s\n", i + 1, subject);
    }
}

void read_single_message(int sock) {
    char username[USER_LEN + 2];
    char msg_num_str[10];
    
    printf("--- Read Message ---\n");
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';
    
    printf("Message number: ");
    fgets(msg_num_str, sizeof(msg_num_str), stdin);
    msg_num_str[strcspn(msg_num_str, "\n")] = '\0';
    
    // Befehl an Server senden
    write(sock, CMD_READ, strlen(CMD_READ));
    write(sock, "\n", 1);
    write(sock, username, strlen(username));
    write(sock, "\n", 1);
    write(sock, msg_num_str, strlen(msg_num_str));
    write(sock, "\n", 1);
    
    // Antwort vom Server lesen
    char response[10];
    read_server_line(sock, response, sizeof(response));
    
    if (strcmp(response, "OK") == 0) 
    {
        char line_buffer[LINE_LEN];
        printf("\n--- Message Content ---\n");
        while (1) {
            read_server_line(sock, line_buffer, sizeof(line_buffer));
            if (strcmp(line_buffer, ".") == 0) 
            {
                break;
            }
            printf("%s\n", line_buffer);
        }
        printf("--- End of Message ---\n");
    } 
    else 
    {
        printf("Error: Could not read message\n");
    }
}

void delete_message(int sock) {
    char username[USER_LEN + 2];
    char msg_num_str[10];
    
    printf("--- Delete Message ---\n");
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';
    
    printf("Message number: ");
    fgets(msg_num_str, sizeof(msg_num_str), stdin);
    msg_num_str[strcspn(msg_num_str, "\n")] = '\0';
    
    // Befehl an Server senden
    write(sock, CMD_DEL, strlen(CMD_DEL));
    write(sock, "\n", 1);
    write(sock, username, strlen(username));
    write(sock, "\n", 1);
    write(sock, msg_num_str, strlen(msg_num_str));
    write(sock, "\n", 1);
    
    // Antwort vom Server lesen
    char response[LINE_LEN];
    read_server_line(sock, response, sizeof(response));
    printf("Server: %s\n", response);
}

int main(int argc, char *argv[]) {
    // Überprüfung der Parameter
    if (argc != 3) {
        printf("Usage: %s <server-ip> <port>\n", argv[0]);
        printf("Example: %s localhost 8080\n", argv[0]);
        return 1;
    }
    
    char* server_ip = argv[1];
    int port = atoi(argv[2]);
    
    // Verbindung zum Server herstellen
    int sock = connect_to_server(server_ip, port);
    if (sock < 0) {
        return 1;
    }
    
    printf("Successfully connected to mail server %s:%d\n", server_ip, port);
  
    while (1) {
        printf("\n========== Mail Client ==========\n");
        printf("1. Send Message\n");
        printf("2. List Messages\n"); 
        printf("3. Read Message\n");
        printf("4. Delete Message\n");
        printf("5. Quit\n");
        printf("Choose: ");
        
        char choice[10];
        fgets(choice, sizeof(choice), stdin);
        
        switch (choice[0]) {
            case '1':
                send_message_to_server(sock);
                break;
            case '2':
                list_user_messages(sock);
                break;
            case '3':
                read_single_message(sock);
                break;
            case '4':
                delete_message(sock);
                break;
            case '5':
                write(sock, CMD_QUIT, strlen(CMD_QUIT));
                write(sock, "\n", 1);
                close(sock);
                printf("Goodbye!\n");
                return 0;
            default:
                printf("Invalid choice\n");
        }
    }
    
    close(sock);
    return 0;
}