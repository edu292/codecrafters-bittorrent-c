#include "bencode.h"
#include "hex.h"
#include "peer.h"
#include "torrent.h"
#include "tracker.h"
#include <curl/curl.h>
#include <inttypes.h>
#include <openssl/crypto.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef bool (*CmdHandler)(int argc, char *argv[]);

typedef struct {
  const char *name;
  CmdHandler handler;
} CommandEntry;

static void print_usage(const char *prog_name) {
  fprintf(
      stderr,
      "Usage: %s <command> [options] <arguments>\n\n"
      "Commands:\n"
      "  decode <bencoded_string>                  Parse and print bencode as "
      "JSON\n"
      "  info <torrent_file>                      Display metainfo from a "
      ".torrent file\n"
      "  peers <torrent_file>                     Query tracker and list peer "
      "IP:port pairs\n"
      "  handshake <torrent_file> <ip:port>       Initiate handshake with peer "
      "and print peer ID\n"
      "  download_piece -o <out> <torrent> <idx>  Download a specific piece to "
      "a file\n"
      "  download -o <out> <torrent>              Download the entire file\n",
      prog_name);
}

static bool load_torrent_or_log(Torrent *t, const char *path) {
  if (!torrent_from_file(t, path)) {
    fprintf(stderr, "Error: Failed to load torrent file: %s\n", path);
    return false;
  }
  return true;
}

static bool cmd_decode(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s decode <bencoded_string>\n", argv[0]);
    return false;
  }

  Bencode bencode;
  if (!bencode_init(&bencode, argv[2])) {
    fprintf(stderr, "Error: Failed to parse bencoded input\n");
    return false;
  }

  char *bencode_str = bencode_to_str(&bencode);
  if (!bencode_str) {
    bencode_destroy(&bencode);
    fprintf(stderr, "Error: Failed to serialize bencode to string\n");
    return false;
  }

  printf("%s\n", bencode_str);
  free(bencode_str);
  bencode_destroy(&bencode);
  return true;
}

static bool cmd_info(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s info <torrent_file>\n", argv[0]);
    return false;
  }

  Torrent torrent;
  if (!load_torrent_or_log(&torrent, argv[2])) {
    return false;
  }

  printf("Tracker URL: %s\n", torrent.tracker_url);
  printf("Length: %" PRId64 "\n", torrent.info->length);
  printf("Info Hash: ");
  print_hex(torrent.info_hash.bytes, sizeof(Sha1Hash));
  printf("\n");
  printf("Piece Length: %zu\n", torrent.info->piece_length);
  printf("Piece Hashes:\n");
  for (size_t i = 0; i < torrent.info->piece_count; i++) {
    print_hex(torrent.info->pieces[i].bytes, sizeof(Sha1Hash));
    printf("\n");
  }

  torrent_destroy(&torrent);
  return true;
}

static bool cmd_peers(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s peers <torrent_file>\n", argv[0]);
    return false;
  }

  Torrent torrent;
  if (!load_torrent_or_log(&torrent, argv[2])) {
    return false;
  }

  PeerVector peers;
  PeerVector_init(&peers, 16);
  if (!tracker_get_peers(&torrent, &peers)) {
    fprintf(stderr, "Error: Failed to retrieve peers from tracker\n");
    PeerVector_destroy(&peers);
    torrent_destroy(&torrent);
    return false;
  }

  for (size_t i = 0; i < peers.count; i++) {
    printf("%s:%u\n", peers.data[i].ip, peers.data[i].port);
  }

  PeerVector_destroy(&peers);
  torrent_destroy(&torrent);
  return true;
}

static bool cmd_handshake(int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s handshake <torrent_file> <peer_ip:port>\n",
            argv[0]);
    return false;
  }

  Torrent torrent;
  if (!load_torrent_or_log(&torrent, argv[2])) {
    return false;
  }

  Peer peer;
  if (!peer_from_str(&peer, argv[3])) {
    fprintf(stderr, "Error: Invalid peer address '%s' (expected <ip:port>)\n",
            argv[3]);
    torrent_destroy(&torrent);
    return false;
  }

  PeerConnection connection;
  if (!peer_connect(&peer, &torrent, &connection)) {
    fprintf(stderr, "Error: Failed to establish peer connection with %s\n",
            argv[3]);
    torrent_destroy(&torrent);
    return false;
  }

  printf("Peer ID: ");
  print_hex(connection.peer_id, sizeof(connection.peer_id));
  printf("\n");

  peer_disconnect(&connection);
  torrent_destroy(&torrent);
  return true;
}

static bool cmd_download_piece(int argc, char *argv[]) {
  const char *out_path = nullptr;
  int opt;
  optind = 2;

  while ((opt = getopt(argc, argv, "o:")) != -1) {
    if (opt == 'o') {
      out_path = optarg;
    } else {
      fprintf(stderr,
              "Usage: %s download_piece -o <output_path> <torrent_file> "
              "<piece_index>\n",
              argv[0]);
      return false;
    }
  }

  if (!out_path || (argc - optind) < 2) {
    fprintf(stderr,
            "Error: Missing required arguments\n"
            "Usage: %s download_piece -o <output_path> <torrent_file> "
            "<piece_index>\n",
            argv[0]);
    return false;
  }

  const char *torrent_path = argv[optind];
  char *endptr = nullptr;
  long piece_index = strtol(argv[optind + 1], &endptr, 10);

  if (*endptr != '\0' || piece_index < 0) {
    fprintf(stderr, "Error: Invalid piece index '%s'\n", argv[optind + 1]);
    return false;
  }

  Torrent torrent;
  if (!load_torrent_or_log(&torrent, torrent_path)) {
    return false;
  }

  if ((size_t)piece_index >= torrent.info->piece_count) {
    fprintf(stderr, "Error: Piece index %ld out of bounds (max %zu)\n",
            piece_index, torrent.info->piece_count - 1);
    torrent_destroy(&torrent);
    return false;
  }

  PeerVector peers;
  PeerVector_init(&peers, 16);
  if (!tracker_get_peers(&torrent, &peers) || peers.count == 0) {
    fprintf(stderr, "Error: No peers available from tracker\n");
    PeerVector_destroy(&peers);
    torrent_destroy(&torrent);
    return false;
  }

  PeerConnection connection;
  if (!peer_connect(&peers.data[0], &torrent, &connection)) {
    fprintf(stderr, "Error: Failed to connect to peer %s:%u\n",
            peers.data[0].ip, peers.data[0].port);
    PeerVector_destroy(&peers);
    torrent_destroy(&torrent);
    return false;
  }

  bool ok = peer_download_piece_to_file(&connection, torrent.info,
                                        (size_t)piece_index, out_path);
  if (ok) {
    printf("Piece %ld downloaded to %s.\n", piece_index, out_path);
  }

  peer_disconnect(&connection);
  PeerVector_destroy(&peers);
  torrent_destroy(&torrent);
  return ok;
}

static bool cmd_download(int argc, char *argv[]) {
  const char *out_path = nullptr;
  int opt;
  optind = 2;

  while ((opt = getopt(argc, argv, "o:")) != -1) {
    if (opt == 'o') {
      out_path = optarg;
    } else {
      fprintf(stderr, "Usage: %s download -o <output_path> <torrent_file>\n",
              argv[0]);
      return false;
    }
  }

  if (!out_path || (argc - optind) < 1) {
    fprintf(stderr,
            "Error: Missing required arguments\n"
            "Usage: %s download -o <output_path> <torrent_file>\n",
            argv[0]);
    return false;
  }

  const char *torrent_path = argv[optind];

  Torrent torrent;
  if (!load_torrent_or_log(&torrent, torrent_path)) {
    return false;
  }

  bool ok = torrent_download_to_file(&torrent, out_path);
  if (ok) {
    printf("Downloaded %s to %s.\n", torrent_path, out_path);
  } else {
    fprintf(stderr, "Error: Failed to download torrent to %s\n", out_path);
  }

  torrent_destroy(&torrent);
  return ok;
}

static const CommandEntry COMMANDS[] = {
    {"decode", cmd_decode},
    {"info", cmd_info},
    {"peers", cmd_peers},
    {"handshake", cmd_handshake},
    {"download_piece", cmd_download_piece},
    {"download", cmd_download},
};

static const CommandEntry *find_command(const char *name) {
  size_t count = sizeof(COMMANDS) / sizeof(COMMANDS[0]);
  for (size_t i = 0; i < count; i++) {
    if (strcmp(COMMANDS[i].name, name) == 0) {
      return &COMMANDS[i];
    }
  }
  return nullptr;
}

int main(int argc, char *argv[]) {
  setbuf(stdout, nullptr);
  setbuf(stderr, nullptr);

  if (argc < 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  const CommandEntry *entry = find_command(argv[1]);
  if (!entry) {
    fprintf(stderr, "Error: Unknown command '%s'\n\n", argv[1]);
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  bool ok = entry->handler(argc, argv);

  OPENSSL_cleanup();
  curl_global_cleanup();

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
