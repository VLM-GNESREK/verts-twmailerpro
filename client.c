#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "Headers/common.h"

char session_user[USER_LEN + 2] = "";

int connect_to_server(const char* server_ip, int port) 
{

    // Localhost mit IP ersetzen

    if (strcmp(server_ip, "localhost") == 0) 
    {
        server_ip = "127.0.0.1";
    }

    // Socket erstellen

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) 
    {
        printf("Error: Socket could not be created\n");
        return -1;
    }
    
    // Server Adresse vorbereiten

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    // IP Adresse umwandeln

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) 
    {
        printf("Fehler: Ungültige Server Addresse\n");
        close(sock);
        return -1;
    }
    
    // Verbindung herstellen

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
    {
        printf("Error: Connection to server failed\n");
        close(sock);
        return -1;
    }
    
    return sock;
}

void read_server_line(int sock, char* buffer, int size) 
{
    int i = 0;
    char c;
    
    while (i < size - 1 && read(sock, &c, 1) > 0) 
    {
        if (c == '\n') break;
        buffer[i++] = c;
    }
    buffer[i] = '\0';
}

int perform_login(int sock)
{
    char username[USER_LEN + 2];
    char password[LINE_LEN];
    char response[LINE_LEN];

    printf("--- Login ---\n");
    printf("Benutzername: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';
    printf("Passwort: ");
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = '\0';

    write(sock, CMD_LOGIN, strlen(CMD_LOGIN));
    write(sock, "\n", 1);
    write(sock, username, strlen(username));
    write(sock, "\n", 1);
    write(sock, password, strlen(password));
    write(sock, "\n", 1);

    read_server_line(sock, response, sizeof(response));

    if(strcmp(response, RESP_OK) == 0)
    {
        printf("Login erfolgreich.\n");
        strcpy(session_user, username);
        return 1;
    }
    else
    {
        printf("Login fehlgeschlagen.\n");
        return 0;
    }
}

void send_message_to_server(int sock) 
{
    char receiver[USER_LEN + 2]; 
    char subject[SUBJECT_LEN + 2];
    char line[LINE_LEN];
    
    printf("--- Sende Nachricht  ---\n");
    
    // Benutzerdaten eingeben
    
    printf("Empfänger: ");
    fgets(receiver, sizeof(receiver), stdin);
    receiver[strcspn(receiver, "\n")] = '\0';
    
    printf("Betreff: ");
    fgets(subject, sizeof(subject), stdin);
    subject[strcspn(subject, "\n")] = '\0';
    
    // Befehl an Server senden

    write(sock, CMD_SEND, strlen(CMD_SEND));
    write(sock, "\n", 1);
    write(sock, receiver, strlen(receiver));
    write(sock, "\n", 1);
    write(sock, subject, strlen(subject));
    write(sock, "\n", 1);
    
    // Nachrichtentext eingeben

    printf("Gib deine Nachricht ein (Beende mit einen '.' oder einer leeren Linie.):\n");
    while (1) 
    {
        fgets(line, sizeof(line), stdin);
        
        if (strcmp(line, ".\n") == 0) 
        {
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

void list_user_messages(int sock) 
{   
    printf("--- List Messages ---\n");
    printf("Username: %s", session_user);
    
    // Befehl an Server senden

    write(sock, CMD_LIST, strlen(CMD_LIST));
    write(sock, "\n", 1);
    
    // Anzahl der Nachrichten lesen

    char count_str[32];
    read_server_line(sock, count_str, sizeof(count_str));
    int count = atoi(count_str);
    
    printf("Found %d messages:\n", count);
    
    // Betreffzeilen lesen

    for (int i = 0; i < count; i++) 
    {
        char subject[SUBJECT_LEN + 1];
        read_server_line(sock, subject, sizeof(subject));
        printf("%d. %s\n", i + 1, subject);
    }
}

void read_single_message(int sock) 
{
    char msg_num_str[10];
    
    printf("--- Lese Nachricht  ---\n");
    
    printf("Nachricht Nummer: ");
    fgets(msg_num_str, sizeof(msg_num_str), stdin);
    msg_num_str[strcspn(msg_num_str, "\n")] = '\0';
    
    // Befehl an Server senden

    write(sock, CMD_READ, strlen(CMD_READ));
    write(sock, "\n", 1);
    write(sock, msg_num_str, strlen(msg_num_str));
    write(sock, "\n", 1);
    
    char response[10];
    read_server_line(sock, response, sizeof(response));
    
    if (strcmp(response, "OK") == 0) 
    {
        char line_buffer[LINE_LEN];
        printf("\n--- Inhalt der Nachricht ---\n");
        while (1) 
        {
            read_server_line(sock, line_buffer, sizeof(line_buffer));
            if (strcmp(line_buffer, ".") == 0) break;
            printf("%s\n", line_buffer);
        }
        printf("--- Ende der Nachricht ---\n");
    } 
    else 
    {
        printf("Fehler: Nachricht konnte nicht gelesen werden.\n");
    }
}

void delete_message(int sock) 
{
    char msg_num_str[10];
    
    printf("--- Nachricht löschen ---\n");
    printf("Nachricht Nummer: ");
    fgets(msg_num_str, sizeof(msg_num_str), stdin);
    msg_num_str[strcspn(msg_num_str, "\n")] = '\0';
    
    // Befehl an Server senden

    write(sock, CMD_DEL, strlen(CMD_DEL));
    write(sock, "\n", 1);
    write(sock, msg_num_str, strlen(msg_num_str));
    write(sock, "\n", 1);
    
    // Antwort vom Server lesen

    char response[LINE_LEN];
    read_server_line(sock, response, sizeof(response));
    printf("Server: %s\n", response);
}

int main(int argc, char *argv[]) 
{
    if (argc != 3) 
    {
        printf("Benutzung: %s <server-ip> <port>\n", argv[0]);
        printf("Beispiel: %s localhost 8080\n", argv[0]);
        return 1;
    }
    
    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    
    int sock = connect_to_server(server_ip, port);
    if (sock < 0) 
    {
        return 1;
    }
    
    printf("Verbindung zum Mail Server %s erfolgreich:%d\n", server_ip, port);
  
    int logged_in = 0;
    while(!logged_in)
    {
        printf("Bitte melden Sie sich an.\n");
        printf("1. Login\n");
        printf("2. Beenden\n");
        printf("Wähle: ");
        char c[10];
        fgets(c, sizeof(c), stdin);

        if(c[0] == '1')
        {
            if(perform_login(sock))
            {
                logged_in = 1;
            }
        }
        else if(c[0] == '2')
        {
            write(sock, CMD_QUIT, strlen(CMD_QUIT));
            write(sock, "\n", 1);
            close(sock);
            return 0;
        }
    }

    while (1) 
    {
        printf("\n========== Mail Client ==========\n");
        printf("1. Nachricht senden\n");
        printf("2. Nachricht auflisten\n"); 
        printf("3. Nachricht lesen\n");
        printf("4. Nachricht löschen\n");
        printf("5. Beenden\n");
        printf("Wähle: ");
        
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
                printf("Auf Wiedersehen!\n");
                return 0;
            default:
                printf("Ungültige Wahl\n");
        }
    }
    
    close(sock);
    return 0;
}