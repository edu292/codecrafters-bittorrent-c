#include "bencode.h"
#include "hex.h"
#include "peer.h"
#include "torrent.h"
#include "tracker.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *prog_name) {
  fprintf(
      stderr,
      "Usage: %s <command> [options] <arguments>\n\n"
      "Commands:\n"
      "  decode <bencoded_string>             Parse and print bencode as JSON\n"
      "  info <torrent_file>                  Display metainfo from a .torrent "
      "file\n"
      "  peers <torrent_file>                 Query tracker and list peer "
      "IP:port pairs\n"
      "  handshake <torrent_file> <ip:port>   Initiate handshake with peer and "
      "print peer ID\n"
      "  download_piece -o <out> <torrent> <piece_index>\n"
      "                                       Download a specific piece to an "
      "output file\n",
      prog_name);
}

int main(int argc, char *argv[]) {
  setbuf(stdout, nullptr);
  setbuf(stderr, nullptr);

  if (argc < 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  const char *command = argv[1];

  if (strcmp(command, "decode") == 0) {
    if (argc < 3) {
      fprintf(stderr,
              "Error: Missing bencoded string\n"
              "Usage : %s decode <bencoded_string>\n",
              argv[0]);
      return EXIT_FAILURE;
    }

    Bencode bencode;
    if (!bencode_init(&bencode, argv[2])) {
      fprintf(stderr, "Error: Failed to parse bencoded input\n");
      return EXIT_FAILURE;
    }

    char *bencode_str = bencode_to_str(&bencode);
    if (!bencode_str) {
      bencode_destroy(&bencode);
      fprintf(stderr, "Error: Failed to serialize bencode to string\n");
      return EXIT_FAILURE;
    }

    printf("%s\n", bencode_str);
    free(bencode_str);
    bencode_destroy(&bencode);
    return EXIT_SUCCESS;
  }

  if (strcmp(command, "info") == 0) {
    if (argc < 3) {
      fprintf(stderr,
              "Error: Missing torrent file path\n"
              "Usage: %s info <torrent_file>\n",
              argv[0]);
      return EXIT_FAILURE;
    }

    Torrent torrent;
    if (!torrent_from_file(&torrent, argv[2])) {
      fprintf(stderr, "Error: Failed to load torrent file: %s\n", argv[2]);
      return EXIT_FAILURE;
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
    return EXIT_SUCCESS;
  }

  if (strcmp(command, "peers") == 0) {
    if (argc < 3) {
      fprintf(stderr,
              "Error: Missing torrent file path\n"
              "Usage: %s peers <torrent_file>\n",
              argv[0]);
      return EXIT_FAILURE;
    }

    Torrent torrent;
    if (!torrent_from_file(&torrent, argv[2])) {
      fprintf(stderr, "Error: Failed to load torrent file: %s\n", argv[2]);
      return EXIT_FAILURE;
    }

    PeerVector peers;
    PeerVector_init(&peers, 16);
    if (!tracker_get_peers(&torrent, &peers)) {
      fprintf(stderr, "Error: Failed to retrieve peers from tracker\n");
      PeerVector_destroy(&peers);
      torrent_destroy(&torrent);
      return EXIT_FAILURE;
    }

    for (size_t i = 0; i < peers.count; i++) {
      Peer *peer = &peers.data[i];
      printf("%s:%u\n", peer->ip, peer->port);
    }

    PeerVector_destroy(&peers);
    torrent_destroy(&torrent);
    return EXIT_SUCCESS;
  }

  if (strcmp(command, "handshake") == 0) {
    if (argc < 4) {
      fprintf(stderr,
              "Error: Missing parameters\n"
              "Usage: %s handshake <torrent_file> <peer_ip:port>\n",
              argv[0]);
      return EXIT_FAILURE;
    }

    Torrent torrent;
    if (!torrent_from_file(&torrent, argv[2])) {
      fprintf(stderr, "Error: Failed to load torrent file: %s\n", argv[2]);
      return EXIT_FAILURE;
    }

    Peer peer;
    if (!peer_from_str(&peer, argv[3])) {
      fprintf(stderr, "Error: Invalid peer address '%s' (expected <ip:port>)\n",
              argv[3]);
      torrent_destroy(&torrent);
      return EXIT_FAILURE;
    }

    PeerSession session;
    if (!peer_session_init(&session, &peer, &torrent.info_hash)) {
      fprintf(stderr, "Error: Failed to establish peer session with %s\n",
              argv[3]);
      torrent_destroy(&torrent);
      return EXIT_FAILURE;
    }

    printf("Peer ID: ");
    print_hex(session.peer_id, sizeof(session.peer_id));
    printf("\n");

    torrent_destroy(&torrent);
    return EXIT_SUCCESS;
  }

  if (strcmp(command, "download_piece") == 0) {
    const char *out_path = nullptr;
    int opt;

    optind = 1;

    while ((opt = getopt(argc - 1, argv + 1, "o:")) != -1) {
      switch (opt) {
      case 'o':
        out_path = optarg;
        break;
      default:
        fprintf(stderr,
                "Usage: %s download_piece -o <output_path> <torrent_file> "
                "<piece_index>\n",
                argv[0]);
        return EXIT_FAILURE;
      }
    }

    int remaining_args = (argc - 1) - optind;
    char **pos_argv = (argv + 1) + optind;

    if (out_path == nullptr || remaining_args < 2) {
      fprintf(stderr,
              "Error: Missing required arguments for download_piece\n"
              "Usage: %s download_piece -o <output_path> <torrent_file> "
              "<piece_index>\n",
              argv[0]);
      return EXIT_FAILURE;
    }

    const char *torrent_path = pos_argv[0];
    char *endptr = nullptr;
    long piece_index = strtol(pos_argv[1], &endptr, 10);

    if (*endptr != '\0' || piece_index < 0) {
      fprintf(stderr,
              "Error: Invalid piece index '%s' (must be a positive integer)\n",
              pos_argv[1]);
      return EXIT_FAILURE;
    }

    Torrent torrent;
    if (!torrent_from_file(&torrent, torrent_path)) {
      fprintf(stderr, "Error: Failed to load torrent file: %s\n", argv[2]);
      return EXIT_FAILURE;
    }

    if ((size_t)piece_index >= torrent.info->piece_count) {
      fprintf(stderr, "Error: Piece index %ld out of bounds (max %zu)\n",
              piece_index, torrent.info->piece_count - 1);
      torrent_destroy(&torrent);
      return EXIT_FAILURE;
    }

    PeerVector peers;
    PeerVector_init(&peers, 16);
    if (!tracker_get_peers(&torrent, &peers) || peers.count == 0) {
      fprintf(stderr, "Error: No peers available from tracker\n");
      PeerVector_destroy(&peers);
      torrent_destroy(&torrent);
      return EXIT_FAILURE;
    }

    PeerSession session;
    if (!peer_session_init(&session, &peers.data[0], &torrent.info_hash)) {
      fprintf(stderr, "Error: Failed to connect to peer %s:%u\n",
              peers.data[0].ip, peers.data[0].port);
      PeerVector_destroy(&peers);
      torrent_destroy(&torrent);
      return EXIT_FAILURE;
    }

    bool ok = torrent_download_piece_to_file(&session, torrent.info,
                                             (size_t)piece_index, out_path);
    if (ok) {
      printf("Piece %ld downloaded to %s.\n", piece_index, out_path);
    }

    peer_session_destroy(&session);
    PeerVector_destroy(&peers);
    torrent_destroy(&torrent);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  fprintf(stderr, "Error: Unknown command '%s'\n\n", command);
  print_usage(argv[0]);
  return EXIT_FAILURE;
}
