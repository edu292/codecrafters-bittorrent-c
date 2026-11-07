#include "bencode.h"
#include "hex.h"
#include "peer.h"
#include "torrent.h"
#include "tracker.h"
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/typecheck-gcc.h>
#include <curl/urlapi.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (!torrent_from_file(&torrent, argv[2])) {
      fprintf(stderr, "failed to init torrent\n");
      return EXIT_FAILURE;
    };

    printf("Tracker URL: %s\n", torrent.tracker_url);
    printf("Length: %ld \n", torrent.info->length);
    printf("Info Hash: ");
    print_hex(torrent.info_hash.bytes, sizeof(Sha1Hash));
    printf("\n");
    printf("Piece Length: %ld\n", torrent.info->piece_length);
    printf("Piece Hashes:");
    for (size_t i = 0; i < torrent.info->piece_count; i++) {
      print_hex(torrent.info->pieces[i].bytes, sizeof(Sha1Hash));
      printf("\n");
    }
    torrent_destroy(&torrent);
  } else if (strcmp(command, "peers") == 0) {
    Torrent torrent;
    if (!torrent_from_file(&torrent, argv[2])) {
      fprintf(stderr, "failed to init torrent\n");
      return EXIT_FAILURE;
    }

    PeerVector peers;
    PeerVector_init(&peers, 16);
    if (!tracker_get_peers(&torrent, &peers)) {
      fprintf(stderr, "failed to get trackers\n");
      return EXIT_FAILURE;
    }

    for (size_t i = 0; i < peers.count; i++) {
      Peer *peer = &peers.data[i];
      printf("%s:%u\n", peer->ip, peer->port);
    }
    PeerVector_destroy(&peers);
  } else if (strcmp(command, "handshake") == 0) {
    Torrent torrent;
    if (!torrent_from_file(&torrent, argv[2])) {
      fprintf(stderr, "failed to init torrent\n");
      return EXIT_FAILURE;
    }

    Peer peer;
    if (!peer_from_str(&peer, argv[3])) {
      fprintf(stderr, "invalid peer\n");
      return EXIT_FAILURE;
    }

    PeerSession session;
    if (!peer_session_init(&session, &peer, &torrent.info_hash)) {
      fprintf(stderr, "could not establish session with peer \n");
      return EXIT_FAILURE;
    }

    printf("Peer ID: ");
    print_hex(session.peer_id, sizeof(session.peer_id));
    printf("\n");
  } else {
    fprintf(stderr, "Unknown command: %s\n", command);
    return 1;
  }

  return 0;
}
