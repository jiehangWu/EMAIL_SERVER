/* server.h
 * Handles the creation of a server socket and data sending.
 * Author  : Jonatan Schroeder
 * Modified: Nov 6, 2021
 */

#ifndef _SERVER_H_
#define _SERVER_H_

#include <stdio.h>

void run_server(const char *port, void (*handler)(int));

int send_all(int fd, char buf[], size_t size);

// The __attribute__ in this function allows the compiler to provided
// useful warnings when compiling the code.
int send_formatted(int fd, const char *str, ...)
  __attribute__ ((format(printf, 2, 3)));

#endif
