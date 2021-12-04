#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define HELO "HELO"
#define EHLO "EHLO"
#define MAIL "MAIL"
#define RCPT "RCPT"
#define DATA "DATA"
#define RSET "RSET"
#define VRFY "VRFY"
#define NOOP "NOOP"
#define QUIT "QUIT"

#define MAX_LINE_LENGTH 1024
#define SERVER_READY "220"
#define CONNECTION_ERROR "554"
#define DATA_START "354"
#define OK "250"
#define QUIT_CODE "221"
#define UNSUPPORTED "502"
#define INVALID "500"
#define BAD_SEQUENCE "503"
#define USER_AMBIGUOUS "553"
#define INVALID_ARG "501"
#define USER_NOT_LOCAL "551"

#define CRLF "\r\n"
#define SP " "

static void handle_client(int fd);

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

int is_prefix(char* prefix, char* s) {
  return strncasecmp(prefix, s, strlen(prefix));
}

int is_command_supported(char* command) {
  return 
    strcasecmp(command, HELO) == 0 ||
    strcasecmp(command, EHLO) == 0 ||
    is_prefix(MAIL, command) == 0 ||
    is_prefix(RCPT, command) == 0 ||
    strcasecmp(command, DATA) == 0 ||
    strcasecmp(command, RSET) == 0 ||
    is_prefix(VRFY, command) == 0 ||
    strcasecmp(command, NOOP) == 0 ||
    strcasecmp(command, QUIT) == 0 ||
    strcasecmp("\n", command) == 0;
}

void send_ready_message(int fd, net_buffer_t nb, struct utsname my_uname) {
  // welcome message
  send_formatted(fd, "%s %s Simple Mail Transfer Service Ready\r\n", SERVER_READY, my_uname.nodename);
}

void handle_HELO(int fd, net_buffer_t nb, struct utsname my_uname) {
  send_formatted(fd, "%s %s\r\n", OK, my_uname.nodename);
}

char* get_client(int fd, char* command, int for_mail) {
  if (strchr(command, ' ') == NULL) {
    return NULL;
  }

  char* token = strtok(command, " ");
  token = strtok(NULL, " ");

  if (token == NULL || strchr(token, ':') == NULL) {
    return NULL;
  }

  if (for_mail == 1) {
    if (is_prefix("FROM", token) != 0) {
      return NULL;
    }
  } else {
    if (is_prefix("TO", token) != 0) {
      return NULL;
    }
  }
  
  
  token = strtok(token, ":");
  token = strtok(NULL, ":");

  if (strchr(token, '<') == NULL) {
    return NULL;
  }

  token = strtok(token, "<");

  if (strchr(token, '@') == NULL) {
    return NULL;
  }

  if (token[strlen(token) - 1] != '>') {
    return NULL;
  }

  token[strlen(token) - 1] = '\0';
  return token;
}

void send_OK(int fd) {
  send_formatted(fd, "%s OK\r\n", OK);  
}

void send_BAD_SEQUENCE(int fd) {
  send_formatted(fd, "%s BAD_SEQUENCE\r\n", BAD_SEQUENCE);  
}

void handle_client(int fd) {
  
  char recvbuf[MAX_LINE_LENGTH + 1];
  net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);
  int session_state = 0; // 1 - initialized, 0 - not initialized
  int transaction_state = 0; // 1 - MAIL ACCPETED, 2 - RCPT ACCEPTED

  struct utsname my_uname;
  uname(&my_uname);
  
  /* recipient BE COMPLETED BY THE STUDENT */
  char* sender;
  user_list_t user_list = create_user_list();

  send_ready_message(fd, nb, my_uname);

  while (1) {
    char* command;
    int result = nb_read_line(nb, recvbuf);

    if (result <= 0) {
      nb_destroy(nb);
      destroy_user_list(user_list);
      return;
    }

    command = recvbuf;
    if (strlen(command) >= 4) {
      command = strtok(command, CRLF);
    }

    // if (!is_command_supported(command)) {
    //   send_formatted(fd, "%s\r\n", UNSUPPORTED);
    //   break;
    // }  
    if (is_prefix(HELO, command) == 0 || is_prefix(EHLO, command) == 0) {
    
      handle_HELO(fd, nb, my_uname);
      session_state = 1;

    } else if (is_prefix(MAIL, command) == 0) {

      if (session_state == 1) {
        sender = get_client(fd, command, 1);
        
        if (sender == NULL || strlen(sender) == 0) {
          send_formatted(fd, "%s Invalid argument\r\n", INVALID_ARG);
        } else {
          transaction_state = 1;
          send_OK(fd); 
        }
        
      } else {
        send_BAD_SEQUENCE(fd);
      }
        
    } else if (is_prefix(RCPT, command) == 0) {

      if ((transaction_state != 1 && transaction_state != 2) || session_state == 0 ) {
        send_BAD_SEQUENCE(fd);
      } else {
        char* recipient = get_client(fd, command, 0);
        if (recipient == NULL) {
          send_formatted(fd, "%s Invalid argument\r\n", INVALID_ARG);
        } else {
          if (is_valid_user(recipient, NULL)) {
            add_user_to_list(&user_list, recipient);
            transaction_state = 2;
            send_OK(fd);  
          } else {
            send_formatted(fd, "%s User not local\r\n", USER_NOT_LOCAL);
          }
        }

      }        
    } else if (strcasecmp(command, DATA) == 0) {

      if (session_state == 0 || transaction_state != 2) {
        send_BAD_SEQUENCE(fd);
      }

      send_formatted(fd, "%s Start mail input; end with .\r\n", DATA_START);
      result = nb_read_line(nb, recvbuf);
      
      char text_buffer[MAX_LINE_LENGTH + 1][MAX_LINE_LENGTH + 1];
      int idx = 0;

      char* line;
      line = recvbuf;

      while (idx <= MAX_LINE_LENGTH && result > 0 && strcmp(line, ".\r\n") != 0) {  
        strcpy(text_buffer[idx], recvbuf);

        result = nb_read_line(nb, recvbuf);
        line = recvbuf;
        idx += 1;
      }

      char template[] = "tmpXXXXXX";
      int f = mkstemp(template);
      FILE* tmp = fdopen(f, "wb");
      
      for (int i = 0; i <= MAX_LINE_LENGTH; i++) {
        write(f, text_buffer[i], strlen(text_buffer[i]));
      }

      save_user_mail(template, user_list);
      unlink(template);
      fclose(tmp);
      
      transaction_state = 0;
      send_OK(fd);  

    } else if (strcasecmp(command, RSET) == 0) {
      
      sender = NULL;
      destroy_user_list(user_list);
      user_list = create_user_list();
      transaction_state = 0;

      send_OK(fd);

    } else if ((is_prefix(VRFY, command) == 0)) {
      char* token = strtok(command, " ");

      while (token != NULL && strchr(token, '@') == NULL) {
        token = strtok(NULL, " ");
      }

      char* domain = token;
      domain[strlen(domain) - 1] = '\0';
  
      if (is_valid_user(domain, NULL)) {
        send_formatted(fd, "%s\r\n", domain);
        send_OK(fd);
      } else {
        send_formatted(fd, "%s User ambiguous\r\n", USER_AMBIGUOUS);
      }

    } else if (strcasecmp(command, QUIT) == 0) {
      
      send_formatted(fd, "%s %s Service closing transmission channel\r\n", QUIT_CODE, my_uname.nodename);
      break;

    } else if (is_prefix(NOOP, command) == 0) {
      send_OK(fd);

    } else {
      send_formatted(fd, "%s\r\n", INVALID);
    }
  }
  
  nb_destroy(nb);
  return;
}





