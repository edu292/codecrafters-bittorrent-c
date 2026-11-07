#include "tracker.h"
#include "bencode.h"
#include "peer.h"
#include "vector.h"
#include <curl/curl.h>
#include <curl/urlapi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
DEFINE_VECTOR(char, CharVector)

static size_t write_to_buffer(void *contents, size_t size, size_t nmemb,
                              void *userp) {
  size_t chunk_bytes = size * nmemb;
  CharVector *response = (CharVector *)userp;
  if (!CharVector_extend(response, contents, chunk_bytes)) {
    return 0;
  }

  return chunk_bytes;
}

bool tracker_get_peers(Torrent *t, PeerVector *out) {
  bool success = false;
  CURLU *url = curl_url();
  CURL *handle = curl_easy_init();
  CharVector response;
  CharVector_init(&response, 0);

  if (url == nullptr || handle == nullptr) {
    fprintf(stderr, "Error: Failed to initialize curl\n");
    goto cleanup;
  }

  char query[256];
  int offset = snprintf(query, sizeof(query),
                        "peer_id=codecrafters-torrent&port=6881&uploaded=0&"
                        "downloaded=0&left=%lld&compact=1&info_hash=",
                        (long long)t->info->length);

  for (size_t i = 0; i < 20; i++) {
    offset += snprintf(query + offset, sizeof(query) - offset, "%%%02x",
                       t->info_hash.bytes[i]);
  }

  curl_url_set(url, CURLUPART_URL, t->tracker_url, 0);
  curl_url_set(url, CURLUPART_QUERY, query, CURLU_APPENDQUERY);

  curl_easy_setopt(handle, CURLOPT_CURLU, url);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_to_buffer);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)&response);

  CURLcode res = curl_easy_perform(handle);
  if (res != CURLE_OK) {
    fprintf(stderr, "Error: Tracker request failed: %s\n",
            curl_easy_strerror(res));
    goto cleanup;
  }

  Bencode bencode;
  if (!CharVector_push(&response, '\0') ||
      !bencode_init(&bencode, response.data)) {
    fprintf(stderr, "Error: Failed to parse tracker response\n");
    goto cleanup;
  }

  const BencodeDict *dict = nullptr;
  if (!bencode_get_dict(&bencode, &dict)) {
    fprintf(stderr, "Error: Tracker response is not valid\n");
    goto cleanup_bencode;
  }

  BencodeString peers;
  if (!bencode_dict_get_str(dict, "peers", &peers)) {
    BencodeString failure;
    if (bencode_dict_get_str(dict, "failure reason", &failure)) {
      fprintf(stderr, "Tracker error: %.*s\n", (int)failure.length,
              failure.data);
    } else {
      fprintf(stderr, "Error: Tracker response missing both 'peers' and "
                      "'failure reason'\n");
    }
    goto cleanup_bencode;
  }

  if (peers.length % 6 != 0) {
    fprintf(stderr,
            "Error: Invalid peers length (%zu bytes, expected multiple of 6)\n",
            peers.length);
    goto cleanup_bencode;
  }

  size_t count = peers.length / 6;
  Peer *dest = PeerVector_grow(out, count);
  if (count > 0 && dest == nullptr) {
    fprintf(stderr, "Error: Failed to allocate memory for %zu peers\n", count);
    goto cleanup_bencode;
  }

  const uint8_t *data = (const uint8_t *)peers.data;
  for (size_t i = 0; i < count; i++) {
    peer_from_bytes(&dest[i], data + (i * 6));
  }

  success = true;

cleanup_bencode:
  bencode_destroy(&bencode);

cleanup:
  CharVector_destroy(&response);
  if (handle != nullptr) {
    curl_easy_cleanup(handle);
  }
  if (url != nullptr) {
    curl_url_cleanup(url);
  }
  return success;
}
