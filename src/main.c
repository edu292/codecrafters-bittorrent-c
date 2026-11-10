#include "encoding.h"
#include <corsair/bencode.h>
#include <corsair/magnet.h>
#include <corsair/peer.h>
#include <corsair/torrent.h>
#include <corsair/tracker.h>
#include <corsair/types.h>
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

static bool load_torrent(Torrent *t, const char *source, bool is_magnet) {
  if (is_magnet) {
    return torrent_from_magnet(t, (char *)source);
  }

  if (!torrent_from_file(t, source)) {
    fprintf(stderr, "Error: Failed to load torrent file: %s\n", source);
    return false;
  }

  return true;
}

static void print_torrent_info(const Torrent *t) {
  printf("Tracker URL: %s\n"
         "Length: %lld\n"
         "Info Hash: ",
         t->tracker_url, t->info->length);
  print_hex(t->info_hash.bytes, sizeof(Sha1Hash));
  printf("\nPiece Length: %zu\n"
         "Piece Hashes:\n",
         t->info->piece_length);
  for (size_t i = 0; i < t->info->piece_count; i++) {
    print_hex(t->info->pieces[i].bytes, sizeof(Sha1Hash));
    printf("\n");
  }
}

static bool parse_output_opt(int *argc, char ***argv, const char **out_path) {
  *out_path = nullptr;
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
  return (*out_path != nullptr);
}

static bool parse_piece_args(int argc, char *argv[], const char **out_path,
                             const char **source, size_t *piece_idx) {
  if (!parse_output_opt(&argc, &argv, out_path) || argc < 2) {
    return false;
  }
  *source = argv[0];
  char *endptr = nullptr;
  long idx = strtol(argv[1], &endptr, 10);
  if (*endptr != '\0' || idx < 0) {
    fprintf(stderr, "Error: Invalid piece index '%s'\n", argv[1]);
    return false;
  }
  *piece_idx = (size_t)idx;
  return true;
}

static bool validate_piece_index(size_t piece_idx, size_t total_pieces) {
  if (piece_idx >= total_pieces) {
    fprintf(stderr, "Error: Piece index %zu out of bounds (max %zu)\n",
            piece_idx, total_pieces - 1);
    return false;
  }
  return true;
}

static bool connect_first_tracker_peer(Torrent *t, PeerConnection *conn) {
  PeerVector peers;
  if (!peer_vector_init(&peers, 16)) {
    return false;
  }

  if (!tracker_get_peers(t, &peers) || peers.count == 0) {
    fprintf(stderr, "Error: No peers returned from tracker '%s'\n",
            t->tracker_url);
    peer_vector_destroy(&peers);
    return false;
  }

  bool ok = peer_connect(&peers.data[0], t, conn);
  if (!ok) {
    fprintf(stderr, "Error: Failed to connect to peer %s:%u\n",
            peers.data[0].ip, peers.data[0].port);
  }
  peer_vector_destroy(&peers);
  return ok;
}

static bool cmd_decode(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Bencode bencode;
  if (!bencode_init(&bencode, sv_from_cstr(argv[1]))) {
    fprintf(stderr, "Error: Failed to parse bencoded input\n");
    return false;
  }

  char *str = bencode_to_str(&bencode);
  if (!str) {
    fprintf(stderr, "Error: Failed to serialize bencode\n");
    bencode_destroy(&bencode);
    return false;
  }

  printf("%s\n", str);
  free(str);
  bencode_destroy(&bencode);
  return true;
}

static bool cmd_info(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Torrent t;
  if (!load_torrent(&t, argv[1], false)) {
    return false;
  }

  print_torrent_info(&t);
  torrent_destroy(&t);
  return true;
}

static bool cmd_peers(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Torrent t;
  if (!load_torrent(&t, argv[1], false)) {
    return false;
  }

  PeerVector peers;
  peer_vector_init(&peers, 16);
  bool ok = tracker_get_peers(&t, &peers);
  if (!ok) {
    fprintf(stderr, "Error: Failed to retrieve peers from tracker\n");
  } else {
    for (size_t i = 0; i < peers.count; i++) {
      printf("%s:%u\n", peers.data[i].ip, peers.data[i].port);
    }
  }
  peer_vector_destroy(&peers);
  torrent_destroy(&t);
  return ok;
}

static bool cmd_handshake(int argc, char *argv[]) {
  if (argc < 3) {
    return err_usage(argv[0]);
  }

  Torrent t;
  if (!load_torrent(&t, argv[1], false)) {
    return false;
  }

  Peer peer;
  if (!peer_from_str(&peer, argv[2])) {
    fprintf(stderr, "Error: Invalid peer address '%s' (expected <ip:port>)\n",
            argv[2]);
    torrent_destroy(&t);
    return false;
  }

  PeerConnection conn;
  bool ok = peer_connect(&peer, &t, &conn);
  if (!ok) {
    fprintf(stderr, "Error: Failed to establish handshake with %s:%u\n",
            peer.ip, peer.port);
  } else {
    printf("Peer ID: ");
    print_hex(conn.peer_id.bytes, sizeof(conn.peer_id));
    printf("\n");
    peer_disconnect(&conn);
  }

  torrent_destroy(&t);
  return ok;
}

static bool download_piece_pipeline(const char *source, size_t piece_idx,
                                    const char *out_path, bool is_magnet) {
  Torrent t;
  if (!load_torrent(&t, source, is_magnet)) {
    return false;
  }

  PeerConnection conn;
  if (!connect_first_tracker_peer(&t, &conn)) {
    torrent_destroy(&t);
    return false;
  }

  if (is_magnet && !peer_request_metadata(&conn, &t)) {
    fprintf(stderr, "Error: Failed to fetch metadata from peer\n");
    peer_disconnect(&conn);
    torrent_destroy(&t);
    return false;
  }

  bool ok = validate_piece_index(piece_idx, t.info->piece_count) &&
            peer_download_piece_to_file(&conn, t.info, piece_idx, out_path);

  if (ok) {
    printf("Piece %zu downloaded to %s.\n", piece_idx, out_path);
  }

  peer_disconnect(&conn);
  torrent_destroy(&t);
  return ok;
}

static bool cmd_download_piece(int argc, char *argv[]) {
  const char *cmd_name = argv[0];
  const char *out_path;
  const char *torrent_path;
  size_t piece_idx;
  if (!parse_piece_args(argc, argv, &out_path, &torrent_path, &piece_idx)) {
    return err_usage(cmd_name);
  }
  return download_piece_pipeline(torrent_path, piece_idx, out_path, false);
}

static bool cmd_download(int argc, char *argv[]) {
  const char *cmd_name = argv[0];
  const char *out_path = nullptr;
  if (!parse_output_opt(&argc, &argv, &out_path) || argc < 1) {
    return err_usage(cmd_name);
  }

  Torrent t;
  if (!load_torrent(&t, argv[0], false)) {
    return false;
  }

  bool ok = torrent_download_to_file(&t, out_path);
  if (ok) {
    printf("Downloaded %s to %s.\n", argv[0], out_path);
  } else {
    fprintf(stderr, "Error: Failed to download torrent to %s\n", out_path);
  }
  torrent_destroy(&t);
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
  printf("Tracker URL: %s\nInfo Hash: ", magnet.tracker);
  print_hex(magnet.btih.bytes, sizeof(Sha1Hash));
  printf("\n");
  return true;
}

static bool cmd_magnet_handshake(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Torrent t;
  if (!load_torrent(&t, argv[1], true)) {
    return false;
  }

  PeerConnection conn;
  if (!connect_first_tracker_peer(&t, &conn)) {
    torrent_destroy(&t);
    return false;
  }

  printf("Peer ID: ");
  print_hex(conn.peer_id.bytes, sizeof(conn.peer_id));
  printf("\nPeer Metadata Extension ID: %u\n", conn.extensions_table[1]);

  peer_disconnect(&conn);
  torrent_destroy(&t);
  return true;
}

static bool cmd_magnet_info(int argc, char *argv[]) {
  if (argc < 2) {
    return err_usage(argv[0]);
  }

  Torrent t;
  if (!load_torrent(&t, argv[1], true)) {
    return false;
  }

  PeerConnection conn;
  if (!connect_first_tracker_peer(&t, &conn)) {
    torrent_destroy(&t);
    return false;
  }

  bool ok = peer_request_metadata(&conn, &t);
  if (!ok) {
    fprintf(stderr, "Error: Failed to fetch metadata from peer\n");
  } else {
    print_torrent_info(&t);
  }

  peer_disconnect(&conn);
  torrent_destroy(&t);
  return ok;
}

static bool cmd_magnet_download_piece(int argc, char *argv[]) {
  const char *cmd_name = argv[0];
  const char *out_path;
  const char *magnet_uri;
  size_t piece_idx;
  if (!parse_piece_args(argc, argv, &out_path, &magnet_uri, &piece_idx)) {
    return err_usage(cmd_name);
  }

  return download_piece_pipeline(magnet_uri, piece_idx, out_path, true);
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
    {
        "magnet_info",
        "<magnet_link>",
        "Query tracker, download metadata, and display info",
        cmd_magnet_info,
    },
    {
        "magnet_download_piece",
        "-o <out> <magnet_link> <idx>",
        "Download a specific piece using a magnet link",
        cmd_magnet_download_piece,
    },
};

static void print_usage(const char *prog_name) {
  fprintf(stderr, "Usage: %s <command> [options] <arguments>\n\nCommands:\n",
          prog_name);
  for (size_t i = 0; i < sizeof(COMMANDS) / sizeof(CommandEntry); i++) {
    fprintf(stderr, "  %-18s %-28s %s\n", COMMANDS[i].name,
            COMMANDS[i].args_syntax, COMMANDS[i].description);
  }
}

static const CommandEntry *find_command(const char *name) {
  for (size_t i = 0; i < sizeof(COMMANDS) / sizeof(COMMANDS[0]); i++) {
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

  return entry->handler(argc - 1, &argv[1]) ? EXIT_SUCCESS : EXIT_FAILURE;
}
