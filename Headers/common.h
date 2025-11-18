#ifndef COMMON_H
#define COMMON_H

// Buffer Definitions

#define USER_LEN 8
#define SUBJECT_LEN 80
#define LINE_LEN 1024

// Command Protocol / Options

#define CMD_LOGIN "LOGIN"
#define CMD_SEND "SEND"
#define CMD_LIST "LIST"
#define CMD_READ "READ"
#define CMD_DEL "DEL"
#define CMD_QUIT "QUIT"

// Server Responses

#define RESP_OK "OK"
#define RESP_ERR "ERR"

#endif