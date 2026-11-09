#include "encoding.h"
#include <corsair/bencode.h>
#include <corsair/magnet.h>
#include <corsair/peer.h>
#include <corsair/torrent.h>
#include <corsair/tracker.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef bool (*CmdHandler)(int argc, char *argv[]);

typedef struct {
  const char *name;
  const char *args_syntax;
  const char *description;
  CmdHandler handler;
} CommandEntry;

static const CommandEntry *find_command(const char *name);

static bool err_usage(const char *cmd_name) {
  const CommandEntry *cmd = find_command(cmd_name);
  fprintf(stderr, "Usage: bittorrent %s %s\n", cmd->name, cmd->args_syntax);
  return false;
}

static bool load_torrent_or_log(Torrent *t, const char *path) {
  if (!torrent_from_file(t, path)) {
    fprintf(stderr, "Error: Failed to load torrent file: %s\n", path);
    return false;
  }
  return true;
}

static bool parse_output_opt(int *argc, char ***argv, const char **out_path) {
  *out_path = NULL;
  optind = 1;

  int opt;
  while ((opt = getopt(*argc, *argv, "o:")) != -1) {
    if (opt == 'o') {
      *out_path = optarg;
    } else {
      return false;
    }
  }

  *argc -= optind;
  *argv += optind;
  return (*out_path != NULL);
}

static bool peer_handshake_and_print(const Peer *peer, Torrent *torrent) {
  PeerConnection conn;
  if (!peer_connect(peer, torrent, &conn)) {
    fprintf(stderr, "Error: Failed to establish handshake with %s:%u\n",
            peer->ip, peer->port);
    return false;
  }

  printf("Peer ID: ");
  print_hex(conn.peer_id.bytes, sizeof(conn.peer_id));
  printf("\n");

  peer_disconnect(&conn);
  return true;
}

static bool cmd_decode(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Bencode bencode;
  if (!bencode_init(&bencode, argv[1])) {
    fprintf(stderr, "Error: Failed to parse bencoded input\n");
    return false;
  }

  char *bencode_str = bencode_to_str(&bencode);
  if (!bencode_str) {
    bencode_destroy(&bencode);
    fprintf(stderr, "Error: Failed to serialize bencode\n");
    return false;
  }

  printf("%s\n", bencode_str);
  free(bencode_str);
  return true;
}

static bool cmd_info(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Torrent torrent;
  if (!load_torrent_or_log(&torrent, argv[1])) {
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
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Torrent torrent;
  if (!load_torrent_or_log(&torrent, argv[1])) {
    return false;
  }

  PeerVector peers;
  peer_vector_init(&peers, 16);
  if (!tracker_get_peers(&torrent, &peers)) {
    fprintf(stderr, "Error: Failed to retrieve peers from tracker\n");
    peer_vector_destroy(&peers);
    torrent_destroy(&torrent);
    return false;
  }

  for (size_t i = 0; i < peers.count; i++) {
    printf("%s:%u\n", peers.data[i].ip, peers.data[i].port);
  }

  peer_vector_destroy(&peers);
  torrent_destroy(&torrent);
  return true;
}

static bool cmd_handshake(int argc, char *argv[]) {
  if (argc < 3) {
    return err_usage(argv[0]);
  }

  Torrent torrent;
  if (!load_torrent_or_log(&torrent, argv[1])) {
    return false;
  }

  bool ok = false;

  Peer peer;
  if (!peer_from_str(&peer, argv[2])) {
    fprintf(stderr, "Error: Invalid peer address '%s' (expected <ip:port>)\n",
            argv[2]);
    goto cleanup;
  }

  ok = peer_handshake_and_print(&peer, &torrent);

cleanup:
  torrent_destroy(&torrent);
  return ok;
}

static bool cmd_download_piece(int argc, char *argv[]) {
  const char *cmd_name = argv[0];
  const char *out_path = NULL;

  if (!parse_output_opt(&argc, &argv, &out_path) || argc < 2) {
    return err_usage(cmd_name);
  }

  const char *torrent_path = argv[0];
  char *endptr = NULL;
  long piece_index = strtol(argv[1], &endptr, 10);
  if (*endptr != '\0' || piece_index < 0) {
    fprintf(stderr, "Error: Invalid piece index '%s'\n", argv[1]);
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
  peer_vector_init(&peers, 16);
  if (!tracker_get_peers(&torrent, &peers) || peers.count == 0) {
    fprintf(stderr, "Error: No peers available from tracker\n");
    peer_vector_destroy(&peers);
    torrent_destroy(&torrent);
    return false;
  }

  PeerConnection conn;
  if (!peer_connect(&peers.data[0], &torrent, &conn)) {
    fprintf(stderr, "Error: Failed to connect to peer %s:%u\n",
            peers.data[0].ip, peers.data[0].port);
    peer_vector_destroy(&peers);
    torrent_destroy(&torrent);
    return false;
  }

  bool ok = peer_download_piece_to_file(&conn, torrent.info,
                                        (size_t)piece_index, out_path);
  if (ok) {
    printf("Piece %ld downloaded to %s.\n", piece_index, out_path);
  }

  peer_disconnect(&conn);
  peer_vector_destroy(&peers);
  torrent_destroy(&torrent);
  return ok;
}

static bool cmd_download(int argc, char *argv[]) {
  const char *cmd_name = argv[0];
  const char *out_path = NULL;

  if (!parse_output_opt(&argc, &argv, &out_path) || argc < 1) {
    return err_usage(cmd_name);
  }

  const char *torrent_path = argv[0];

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

static bool cmd_magnet_parse(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Magnet magnet;
  if (!magnet_parse(&magnet, argv[1])) {
    fprintf(stderr, "Error: Failed to parse magnet link: malformed or missing "
                    "required parameters\n");
    return false;
  }

  printf("Tracker URL: %s\n", magnet.tracker);
  printf("Info Hash: ");
  print_hex(magnet.btih.bytes, sizeof(Sha1Hash));
  printf("\n");

  return true;
}

static bool cmd_magnet_handshake(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Torrent torrent;
  if (!torrent_from_magnet(&torrent, argv[1])) {
    return false;
  }

  bool ok = false;
  PeerVector peers;
  peer_vector_init(&peers, 16);
  if (!tracker_get_peers(&torrent, &peers) || peers.count == 0) {
    fprintf(stderr, "Error: No peers returned from tracker '%s'\n",
            torrent.tracker_url);
    goto cleanup;
  }

  ok = peer_handshake_and_print(&peers.data[0], &torrent);

cleanup:
  torrent_destroy(&torrent);
  peer_vector_destroy(&peers);
  return ok;
}

static const CommandEntry COMMANDS[] = {
    {
        "decode",
        "<bencoded_string>",
        "Parse and print bencode as JSON",
        cmd_decode,
    },
    {
        "info",
        "<torrent_file>",
        "Display metainfo from a .torrent file",
        cmd_info,
    },
    {
        "peers",
        "<torrent_file>",
        "Query tracker and list peer IP:port pairs",
        cmd_peers,
    },
    {
        "handshake",
        "<torrent_file> <ip:port>",
        "Initiate peer handshake and print peer ID",
        cmd_handshake,
    },
    {
        "download_piece",
        "-o <out> <torrent> <idx>",
        "Download a specific piece to a file",
        cmd_download_piece,
    },
    {
        "download",
        "-o <out> <torrent>",
        "Download the entire file",
        cmd_download,
    },
    {
        "magnet_parse",
        "<magnet_link>",
        "Parse and print magnet URI info",
        cmd_magnet_parse,
    },
    {
        "magnet_handshake",
        "<magnet_link>",
        "Query tracker and handshake with first peer",
        cmd_magnet_handshake,
    },
};

static void print_usage(const char *prog_name) {
  fprintf(stderr, "Usage: %s <command> [options] <arguments>\n\nCommands:\n",
          prog_name);
  for (size_t i = 0; i < sizeof(COMMANDS) / sizeof(CommandEntry); i++) {
    const CommandEntry *c = &COMMANDS[i];
    fprintf(stderr, "  %-14s %-28s %s\n", c->name, c->args_syntax,
            c->description);
  }
}

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

  bool ok = entry->handler(argc - 1, &argv[1]);

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
