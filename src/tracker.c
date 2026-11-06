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
  curl_url_set(url, CURLUPART_URL, t->tracker_url, 0);
  size_t len = 80;
  char hash_query[len];
  int offset = snprintf(hash_query, len, "info_hash=");
  for (size_t i = 0; i < 20; i++) {
    offset += snprintf(hash_query + offset, len - offset, "%%%02x",
                       t->info_hash.bytes[i]);
  }
  curl_url_set(url, CURLUPART_QUERY, hash_query, CURLU_APPENDQUERY);
  curl_url_set(url, CURLUPART_QUERY, "peer_id=codecrafters-torrent",
               CURLU_APPENDQUERY);
  curl_url_set(url, CURLUPART_QUERY, "port=6881", CURLU_APPENDQUERY);
  curl_url_set(url, CURLUPART_QUERY, "uploaded=0", CURLU_APPENDQUERY);
  curl_url_set(url, CURLUPART_QUERY, "downloaded=0", CURLU_APPENDQUERY);
  char left_query[32];
  snprintf(left_query, sizeof(left_query), "left=%lld", t->info->length);
  curl_url_set(url, CURLUPART_QUERY, left_query, CURLU_APPENDQUERY);
  curl_url_set(url, CURLUPART_QUERY, "compact=1", CURLU_APPENDQUERY);
  CURL *handle = curl_easy_init();
  curl_easy_setopt(handle, CURLOPT_CURLU, url);

  CharVector response;
  CharVector_init(&response, 0);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_to_buffer);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)&response);
  CURLcode res = curl_easy_perform(handle);
  if (res != CURLE_OK) {
    fprintf(stderr, "nok");
    goto cleanup;
  }

  Bencode bencode;
  if (!CharVector_push(&response, '\0') ||
      !bencode_init(&bencode, response.data)) {
    fprintf(stderr, "a");
    goto cleanup;
  }

  Bencode peers;
  if (!BencodeDict_get(bencode.value.dict, bstr("peers"), &peers)) {
    Bencode failure;
    BencodeDict_get(bencode.value.dict, bstr("failure reason"), &failure);
    fprintf(stderr, "Tracker error: %.*s\n", (int)failure.value.string.length,
            failure.value.string.data);
    goto cleanup_bencode;
  }

  const uint8_t *data = (const uint8_t *)peers.value.string.data;
  size_t count = peers.value.string.length / 6;

  Peer *dest = PeerVector_grow(out, count);
  if (count > 0 && dest == nullptr) {
    fprintf(stderr, "d");
    goto cleanup_bencode;
  }

  for (size_t i = 0; i < count; i++) {
    peer_init(data + (i * 6), &dest[i]);
  }

  success = true;

cleanup_bencode:
  bencode_destroy(&bencode);

cleanup:
  CharVector_destroy(&response);
  curl_easy_cleanup(handle);
  curl_url_cleanup(url);
  return success;
}
