/* netbuffer.c
 * Provides an alternative method for reading strings from a socket
 * file descriptor based on a stdio-style buffer.
 * Author  : Jonatan Schroeder
 * Modified: Nov 6, 2021
 */

#include "netbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

struct net_buffer {
  int    fd;
  size_t max_bytes;
  size_t avail_data;
  // Buffer set as size zero, but since it's the last member of the
  // struct, it is possible to malloc additional memory after this
  // struct to be used as part of the buffer (e.g., nb->buf[5] will
  // read from a location 5 bytes ahead of the end of the buffer).
  char   buf[0];
};

/** Creates a new buffer for handling data read from a socket.
 *
 *  Note: The maximum buffer size passed as parameter will also
 *  correspond to the maximum number of bytes other functions (like
 *  nb_read_line) can return at a time, so it is advisable to make
 *  this size at least as big as the maximum line size for the
 *  protocol handled in this socket.
 *  
 *  Parameters: fd: Socket file descriptor.
 *              max_buffer_size: Maximum number of bytes to be stored
 *                               locally for a connection. 
 *
 *  Returns: A net_buffer_t object that can be used in other functions
 *           to read buffered data.
 */
net_buffer_t nb_create(int fd, size_t max_buffer_size) {

  net_buffer_t nb = malloc(sizeof(struct net_buffer) + max_buffer_size);
  nb->fd          = fd;
  nb->max_bytes   = max_buffer_size;
  nb->avail_data  = 0;
  return nb;
}

/** Frees all memory used by a net_buffer_t object.
 *  
 *  Parameters: nb: buffer object to be freed.
 */
void nb_destroy(net_buffer_t nb) {
  free(nb);
}

/** Reads a single line from the socket/buffer (i.e., a string ending
 *  in LF, aka "\n"). If the socket returns more than one line in a
 *  single call to recv, returns a single line and caches the
 *  remaining data for the next call. If the socket returns part of a
 *  line in a single call to recv, calls recv repeatedly until a full
 *  line is received or the buffer is full.
 *
 *  The returned string is null-terminated, which allows the out
 *  buffer to the handled as a regular string. Note, though, that this
 *  function does not check for null bytes found in the middle of the
 *  string.
 *
 *  If a line with more than max_buffer_size bytes is read, then
 *  returns the first max_buffer_size bytes (with a terminating null
 *  byte). The caller may identify the case by checking if the last
 *  character in the string is not LF.
 *
 *  Parameter: nb: buffer object where socket and cache data are stored.
 *             out: array of bytes where the read line will be
 *                  stored. It must have space for at least
 *                  max_buffer_size bytes (from nb_create function)
 *                  plus one (for terminating null byte).
 *
 *  Returns: If the connection was terminated properly, returns 0. If
 *           the connection was terminated abruptly or another unknown
 *           error is found, returns -1. Otherwise, returns the number
 *           of bytes in the read line.
 */
int nb_read_line(net_buffer_t nb, char out[]) {

  char *eos;
  int rv;
  // Check if the buffer already has a line-feed character.
  while ((eos = memchr(nb->buf, '\n', nb->avail_data)) == NULL) {

    // Check if the buffer has space for more data to be received
    if (nb->avail_data < nb->max_bytes) {
      rv = recv(nb->fd, nb->buf + nb->avail_data, nb->max_bytes - nb->avail_data, 0);
      // If recv returns an error, return the same error.
      if (rv < 0)
	return rv;
      // If recv returns 0 (i.e., end of data), return whatever is
      // available in the buffer.
      if (rv == 0) {
	eos = nb->buf + nb->avail_data - 1;
	break;
      }
      nb->avail_data += rv;
    } else {
      // If the buffer is already full, return the full buffer.
      eos = nb->buf + nb->max_bytes - 1;
      break;
    }
  }

  // Copy received data from the buffer to the output.
  rv = eos - nb->buf + 1;
  memcpy(out, nb->buf, rv);
  out[rv] = 0;
  nb->avail_data -= rv;
  // If the received data contains more than one line, move the
  // remaining data to the start of the buffer.
  if (nb->avail_data)
    memmove(nb->buf, eos + 1, nb->avail_data);
  return rv;
}
