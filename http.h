/*
 * Copyright (c) 2020-2022 joshua stein <jcs@jcs.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __HTTP_H__
#define __HTTP_H__

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "util.h"

struct url {
	char *scheme;
	char *host;
	unsigned short port;
	char *path;
};

struct http_request {
	struct url *url;

	int socket;

	char *message;
	int status;

	char chunk[2048];
	ssize_t chunk_len;
	ssize_t chunk_off;
};

struct url * url_parse(const char *str);
char * url_encode(unsigned char *str);

struct http_request * http_get(const char *url);
ssize_t http_req_read(struct http_request *req, char *data, size_t len);
int http_req_skip_header(struct http_request *req);
char http_req_byte_peek(struct http_request *req);
char http_req_byte_read(struct http_request *req);
char *http_req_chunk_peek(struct http_request *req);
char *http_req_chunk_read(struct http_request *req);
void http_req_free(struct http_request *req);

#endif
