#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
#define POSITIVE "+OK"
#define NEGATIVE "-ERR"

#define USER "USER"
#define PASS "PASS"
#define STAT "STAT"
#define LIST "LIST"
#define RETR "RETR"
#define DELE "DELE"
#define RSET "RSET"
#define NOOP "NOOP"
#define QUIT "QUIT"

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
  return strncasecmp(prefix, s, strlen(prefix)) == 0;
}

int compare(char* s1, char* s2) {
  return strcasecmp(s1, s2) == 0;
}

int is_command_supported(char* command) {
  return 
    is_prefix(USER, command) ||
    is_prefix(PASS, command) ||
    compare(STAT, command) ||
    is_prefix(LIST, command) ||
    is_prefix(RETR, command) ||
    is_prefix(DELE, command) ||
    compare(RSET, command) ||
    compare(NOOP, command) ||
    compare(QUIT, command) ||
    compare("\n", command);
}

void send_OK(int fd) {
  send_formatted(fd, "%s %s\r\n", POSITIVE, "Good");
}

void send_ERR(int fd) {
  send_formatted(fd, "%s %s\r\n", NEGATIVE, "Bad");
}

void send_ready_message(int fd) {
  send_formatted(fd, "+OK POP3 server ready\r\n");
}

int check_transactions_state(int fd, int transaction_state) {
  if (transaction_state != 1) {
    send_ERR(fd);
    return 0;
  }

  return 1;
}

char* get_argument(char* command) {
  if (strchr(command, ' ') == NULL) {
    return NULL;
  }

  char* token = strtok(command, " ");
  token = strtok(NULL, " ");

  if (token == NULL) {
    return NULL;
  }

  char* newToken = malloc(strlen(token));
  
  strcpy(newToken, token);
  return newToken;
}

void list_mail_items(int fd, mail_list_t list) {
  int mail_count = get_mail_count(list);
  int count = 0;
  int i = 0;

  while (count < mail_count) {
    mail_item_t item = get_mail_item(list, i);
    if (item != NULL) {
      int size = get_mail_item_size(item);
      count += 1;
      send_formatted(fd, "%d %d\r\n", i + 1, size);
    }
    
    i += 1;
  }

  send_formatted(fd, ".\r\n");
}

void handle_client(int fd) {
  
  char recvbuf[MAX_LINE_LENGTH + 1];
  net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);

  int auth_state = 0; // 1 - AUTHORIZATION 2 - USER ACCEPTED
  int transaction_state = 0;
  char* user_name;
  mail_list_t mail_list;
  
  /* TO BE COMPLETED BY THE STUDENT */
  send_ready_message(fd);

  // Transitioning into AUTHORIZATION state
  auth_state = 1;
  
  while (1) {
    char* command;
    int result = nb_read_line(nb, recvbuf);

    if (result <= 0) {
      nb_destroy(nb);
      return;
    }

    command = recvbuf;
    if (strlen(command) >= 4) {
      command = strtok(command, CRLF);
    }

    if (is_prefix(USER, command)) {

      if (auth_state == 1) {
        user_name = get_argument(command);
        if (user_name == NULL) {
          send_ERR(fd);
        } else {
          // check if user exists
          if (is_valid_user(user_name, NULL)) {
            send_OK(fd);
            auth_state = 2;
          } else {
            send_ERR(fd);
          }
        }
      } else {
        send_ERR(fd);
      }

    } else if (is_prefix(PASS, command)) {

      if (auth_state == 2 && user_name != NULL) {
        char* password = get_argument(command);

        if (password == NULL) {
          send_ERR(fd);
        } else {
          if (is_valid_user(user_name, password)) {
            transaction_state = 1;
            mail_list = load_user_mail(user_name);
            send_OK(fd);
          } else {
            send_ERR(fd);
          }
        }
      } else {
        send_ERR(fd);
      }

    } else if (compare(STAT, command)) {

      if (check_transactions_state(fd, transaction_state)) {
        int mail_count = get_mail_count(mail_list);
        int mail_list_size = get_mail_list_size(mail_list);

        send_formatted(fd, "%s %d %d\r\n", POSITIVE, mail_count, mail_list_size);
      }

    } else if (is_prefix(LIST, command)) {
      
      if (check_transactions_state(fd, transaction_state)) {
        char* positionStr = get_argument(command);

        if (positionStr == NULL) {
          int mail_count = get_mail_count(mail_list);
          int mail_list_size = get_mail_list_size(mail_list);

          send_formatted(fd, "+OK %d messages (%d octets)\r\n", mail_count, mail_list_size);
          
          list_mail_items(fd, mail_list);
        } else {
          int position = atoi(positionStr);

          mail_item_t mail_item = get_mail_item(mail_list, position - 1);

          if (mail_item == NULL) {
            send_ERR(fd);
          } else {
            int mail_size = get_mail_item_size(mail_item);
            send_formatted(fd, "%s %d %d\r\n", POSITIVE, position, mail_size);
          }
        }
      }

    } else if (is_prefix(RETR, command)) {

      if (check_transactions_state(fd, transaction_state)) {
        char* positionStr = get_argument(command);

        if (positionStr == NULL) {
          send_ERR(fd);
          continue;
        }

        int position = atoi(positionStr) - 1;

        mail_item_t mail_item = get_mail_item(mail_list, position);

        if (mail_item == NULL) {
          send_ERR(fd);
        } else {
          int size = get_mail_item_size(mail_item);

          send_formatted(fd, "%s %d octets\r\n", POSITIVE, size);

          FILE* file = get_mail_item_contents(mail_item);
          char line[MAX_LINE_LENGTH + 1];
          char buffer[MAX_LINE_LENGTH * (MAX_LINE_LENGTH + 1)];

          while (fgets(line, sizeof(line), file)) {
            if (strlen(line) == 0) {
              break;
            }
            strcat(buffer, line);
          } 
          send_formatted(fd, "%s", buffer);
          send_formatted(fd, ".\r\n");
        }
      }

    } else if (is_prefix(DELE, command)) {

      if (check_transactions_state(fd, transaction_state)) {
        char* positionStr = get_argument(command);

        if (positionStr == NULL) {
          send_ERR(fd);
          continue;
        }

        int position = atoi(positionStr) - 1;

        mail_item_t mail_item = get_mail_item(mail_list, position);

        if (mail_item == NULL) {
          send_ERR(fd);
        } else {
          mark_mail_item_deleted(mail_item);
          send_formatted(fd, "%s message %d deleted\r\n", POSITIVE, position + 1);
        }
      }

    } else if (compare(NOOP, command)) {

      // check_transactions_state(fd, transaction_state);
      send_OK(fd);

    } else if (compare(RSET, command)) {

      if (check_transactions_state(fd, transaction_state)) {
        int number_of_reset_messages = reset_mail_list_deleted_flag(mail_list);
        send_formatted(fd, "+OK %d messages recovered\r\n", number_of_reset_messages);
      }
      
    } else if (compare(QUIT, command)) {
      send_OK(fd);
      break;
    }
  }
    
  destroy_mail_list(mail_list);
  nb_destroy(nb);
}
