#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum : uint8_t {
  BENCODE_STRING,
  BENCODE_INTEGER,
  BENCODE_LIST,
  BENCODE_DICT,
} BencodeType;

typedef union BencodeValue {
  char *string;
  int64_t integer;
  union BencodeValue *list;
} BencodeValue;

typedef struct {
  BencodeType type;
  BencodeValue value;
} Bencode;

bool is_digit(char c) { return c >= '0' && c <= '9'; }

Bencode decode_bencode(const char *bencoded_value) {
  if (is_digit(bencoded_value[0])) {
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
    return (Bencode){.type = BENCODE_STRING, .value.string = decoded_str};
  }

  switch (bencoded_value[0]) {
  case 'i':
    const char *end_index = strchr(bencoded_value, 'e');
    if (end_index == nullptr) {
      fprintf(stderr, "Invalid encoded value: %s\n", bencoded_value);
      exit(1);
    }

    return (Bencode){.type = BENCODE_INTEGER,
                     .value.integer = atoll(bencoded_value + 1)};
  case 'l':
    fprintf(stderr, "Not implemented\n");
    exit(1);
  case 'd':
    fprintf(stderr, "Not implemented\n");
    exit(1);
  default:
    fprintf(stderr, "Invalid\n");
    exit(1);
  }
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
    Bencode bencode = decode_bencode(encoded_str);
    switch (bencode.type) {
    case BENCODE_STRING:
      printf("\"%s\"\n", bencode.value.string);
      free(bencode.value.string);
      break;
    case BENCODE_INTEGER:
      printf("%ld\n", bencode.value.integer);
      break;
    }
  } else {
    fprintf(stderr, "Unknown command: %s\n", command);
    return 1;
  }

  return 0;
}
