#include "bencode.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
    const char *filepath = argv[2];

    int fd = open(filepath, O_RDONLY);
    struct stat sb;
    fstat(fd, &sb);

    char *data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    Bencode bencode;
    bencode_init(&bencode, data);

    Bencode announce_val;
    BencodeDict_get(bencode.value.dict, "announce", &announce_val);

    Bencode info_dict_val;
    BencodeDict_get(bencode.value.dict, "info", &info_dict_val);

    Bencode length_val;
    BencodeDict_get(info_dict_val.value.dict, "length", &length_val);

    printf("Tracker URL: %s\n", announce_val.value.string);
    printf("Length: %ld \n", length_val.value.integer);

    bencode_destroy(&bencode);
    munmap(data, sb.st_size);
    close(fd);
  } else {
    fprintf(stderr, "Unknown command: %s\n", command);
    return 1;
  }

  return 0;
}
