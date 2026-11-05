#include "bencode.h"
#include "hex.h"
#include "torrent.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

int main(int argc, char *argv[]) {
  setbuf(stdout, nullptr);
  setbuf(stderr, nullptr);

  if (argc < 3) {
    fprintf(stderr, "Usage: your_program.sh <command> <args>\n");
    return 1;
  }

  const char *command = argv[1];

  if (strcmp(command, "decode") == 0) {
    const char *encoded_str = argv[2];
    Bencode bencode;
    bencode_init(&bencode, encoded_str);
    char *bencode_str = bencode_to_str(&bencode);
    printf("%s\n", bencode_str);
    free(bencode_str);
    bencode_destroy(&bencode);
  } else if (strcmp(command, "info") == 0) {
    Torrent torrent;
    torrent_from_file(&torrent, argv[2]);
    printf("Tracker URL: %s\n", torrent.tracker_url);
    printf("Length: %ld \n", torrent.info->length);
    printf("Info Hash: ");
    print_hex(torrent.info_hash, sizeof(torrent.info_hash));
    printf("\n");
  } else {
    fprintf(stderr, "Unknown command: %s\n", command);
    return 1;
  }

  return 0;
}
