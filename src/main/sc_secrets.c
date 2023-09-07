/*
 * sc_secrets.c
 *
 * Copyright (C) 2023 Aerospike, Inc.
 *
 * All rights reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE. THE COPYRIGHT NOTICE ABOVE DOES
 * NOT EVIDENCE ANY ACTUAL OR INTENDED PUBLICATION.
 */

//==========================================================
// Includes.
//

#include "sc_b64.h"
#include "sc_secrets.h"
#include "sc_socket.h"
#include "sc_logging.h"

#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "jansson.h"


//==========================================================
// Typedefs & constants.
//

#define SC_HEADER_SIZE 8
#define SC_MAGIC 0x51dec1cc // "sidekick" in hexspeak
#define SC_MAX_RECV_JSON_SIZE (100 * 1024) // 100KB

//==========================================================
// Globals.
//

static const char TRAILING_WHITESPACE[] = " \t\n\r\f\v";

//==========================================================
// Public API.
//

char*
sc_request_secret(sc_socket* sock, const char* rsrc_sub, uint32_t rsrc_sub_len,
		const char* secret_key, uint32_t secret_key_len, int timeout_ms)
{
	char req[100 + rsrc_sub_len + secret_key_len];
	char* json = &req[SC_HEADER_SIZE]; // json starts after 8 byte header

	if (rsrc_sub_len == 0) {
		sprintf(json, "{\"SecretKey\":\"%.*s\"}", secret_key_len, secret_key);
	}
	else {
		sprintf(json,
				"{\"Resource\":\"%.*s\",\"SecretKey\":\"%.*s\"}",
				rsrc_sub_len, rsrc_sub, secret_key_len, secret_key);
	}

	uint32_t json_sz = (uint32_t)strlen(json);

	assert(SC_HEADER_SIZE + json_sz <= sizeof(req));

	*(uint32_t*)&req[0] = ntohl(SC_MAGIC);
	*(uint32_t*)&req[4] = ntohl(json_sz);

	if (write_n_bytes(sock, SC_HEADER_SIZE + json_sz, req, timeout_ms) <= 0) {
		sc_g_log_function("ERR: failed asking for secret - %s", req);
		return NULL;
	}

	char header[SC_HEADER_SIZE];

	if (read_n_bytes(sock, SC_HEADER_SIZE, header, timeout_ms) <= 0) {
		sc_g_log_function("ERR: failed reading secret header errno: %d", errno);
		return NULL;
	}

	uint32_t recv_magic = ntohl(*(uint32_t*)&header[0]);

	if (recv_magic != SC_MAGIC) {
		sc_g_log_function("ERR: bad magic - %x", recv_magic);
		return NULL;
	}

	uint32_t recv_json_sz = ntohl(*(uint32_t*)&header[4]);

	if (recv_json_sz > SC_MAX_RECV_JSON_SIZE) {
		sc_g_log_function("ERR: response too big - %d", recv_json_sz);
		return NULL;
	}

	char *recv_json = malloc(recv_json_sz + 1);

	if (read_n_bytes(sock, recv_json_sz, recv_json, timeout_ms) <= 0) {
		sc_g_log_function("ERR: failed reading secret errno: %d", errno);
		return NULL;
	}

	recv_json[recv_json_sz] = '\0';

	return recv_json;
}

uint8_t*
sc_parse_json(const char* json_buf, size_t* size_r)
{
	if (json_buf == NULL) {
		return NULL;
	}

	json_error_t err;

	json_t* doc = json_loads(json_buf, 0, &err);

	if (doc == NULL) {
		sc_g_log_function("ERR: failed to parse response JSON line %d (%s)",
				err.line, err.text);
		return NULL;
	}

	const char* payload_str;
	size_t payload_len;

	int unpack_err = json_unpack(doc, "{s:s%}", "Error", &payload_str,
			&payload_len);

	// If secret agent faced an error it will convey the reason.
	if (unpack_err == 0) {
		sc_g_log_function("ERR: response: %.*s",
				(int)payload_len, payload_str);
		json_decref(doc);
		return NULL;
	}

	unpack_err = json_unpack(doc, "{s:s%}", "SecretValue", &payload_str,
			&payload_len);

	if (unpack_err != 0) {
		sc_g_log_function("ERR: failed to find \"SecretValue\" in response");
		json_decref(doc);
		return NULL;
	}

	if (payload_len == 0) {
		sc_g_log_function("ERR: empty secret");
		json_decref(doc);
		return NULL;
	}

	while (strchr(TRAILING_WHITESPACE, payload_str[payload_len - 1]) != NULL) {
		payload_len--;

		if (payload_len == 0) {
			sc_g_log_function("ERR: whitespace-only secret");
			json_decref(doc);
			return NULL;
		}
	}

	// Extra byte - if this is a string, the caller will add '\0'.
	uint32_t size = sc_b64_decoded_buf_size((uint32_t)payload_len) + 1;

	uint8_t* buf = malloc(size);

	if (! sc_b64_validate_and_decode(payload_str, (uint32_t)payload_len, buf,
			&size)) {
		sc_g_log_function("ERR: failed to base64-decode secret");
		free(buf);
		json_decref(doc);
		return NULL;
	}

	json_decref(doc);

	*size_r = size;
	return buf;
}