#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool is_digit(char c) { return c >= '0' && c <= '9'; }

char *decode_bencode(const char *bencoded_value) {
  if (!is_digit(bencoded_value[0])) {
    fprintf(stderr, "Only strings are supported at the moment\n");
    exit(1);
  }

  int length = atoi(bencoded_value);
  const char *colon_index = strchr(bencoded_value, ':');
  if (colon_index == nullptr) {
    fprintf(stderr, "Invalid encoded value: %s\n", bencoded_value);
    exit(1);
  }

  const char *start = colon_index + 1;

  char *decoded_str = malloc(length + 1);
  strncpy(decoded_str, start, length);
  decoded_str[length] = '\0';
  return decoded_str;
}

int main(int argc, char *argv[]) {
  setbuf(stdout, nullptr);
  setbuf(stderr, nullptr);

  if (argc < 3) {
    fprintf(stderr, "Usage: your_program.sh <command> <args>\n");
    return 1;
  }

  const char *command = argv[1];

  if (strcmp(command, "decode") == 0) {
    fprintf(stderr, "Logs from your program will appear here!\n");

    const char *encoded_str = argv[2];
    char *decoded_str = decode_bencode(encoded_str);
    printf("\"%s\"\n", decoded_str);
    free(decoded_str);
  } else {
    fprintf(stderr, "Unknown command: %s\n", command);
    return 1;
  }

  return 0;
}
