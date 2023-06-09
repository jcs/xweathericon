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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include "http.h"

extern char *__progname;

struct url *
url_parse(const char *str)
{
	struct url *url = NULL;
	char *scheme, *host, *path;
	unsigned int port;
	int ret, pos;
	size_t len, schemelen, hostlen, pathlen;

	len = strlen(str);
	scheme = malloc(len + 1);
	if (scheme == NULL)
		err(1, "malloc");
	host = malloc(len + 1);
	if (host == NULL)
		err(1, "malloc");
	path = malloc(len + 1);
	if (path == NULL)
		err(1, "malloc");

	/* scheme://host:port/path */
	ret = sscanf(str, "%[^:]://%[^:]:%u%s%n", scheme, host, &port, path,
	    &pos);
	if (ret == 4) {
		if (pos > len)
			errx(1, "url_parse sscanf overflow");
		goto consolidate;
	}

	/* scheme://host/path */
	ret = sscanf(str, "%[^:]://%[^/]%s%n", scheme, host, path, &pos);
	if (ret == 3) {
		if (pos > len)
			errx(1, "url_parse sscanf overflow");
		if (strcmp(scheme, "http") == 0)
			port = 80;
		else if (strcmp(scheme, "https") == 0)
			port = 443;
		else
			goto cleanup;
		goto consolidate;
	}

	goto cleanup;

consolidate:
	schemelen = strlen(scheme);
	hostlen = strlen(host);
	pathlen = strlen(path);

	/*
	 * Put everything in a single chunk of memory so the caller can just
	 * free(url)
	 */
	len = sizeof(struct url) + schemelen + 1 + hostlen + 1 + pathlen + 1;
	url = malloc(len);
	if (url == NULL)
		err(1, "malloc");

	url->scheme = (char *)url + sizeof(struct url);
	len = strlcpy(url->scheme, scheme, schemelen + 1);

	url->host = url->scheme + len + 1;
	len = strlcpy(url->host, host, hostlen + 1);

	url->path = url->host + len + 1;
	len = strlcpy(url->path, path, pathlen + 1);

	url->port = port;

cleanup:
	free(scheme);
	free(host);
	free(path);

	return url;
}

char *
url_encode(unsigned char *str)
{
	char *ret = NULL;
	size_t len, n;

encode:
	for (n = 0, len = 0; str[n] != '\0'; n++) {
		if ((str[n] >= 'A' && str[n] <= 'Z') ||
		  (str[n] >= 'a' && str[n] <= 'z') ||
		  (str[n] >= '0' && str[n] <= '9') ||
		  (str[n] == '-' || str[n] == '_' || str[n] == '.' ||
		  str[n] == '~')) {
			if (ret)
				ret[len] = str[n];
			len++;
		} else {
			if (ret)
				snprintf(ret + len, 4, "%%%02X", str[n]);
			len += 3;
		}
	}

	if (ret) {
		ret[len] = '\0';
		return ret;
	}

	ret = malloc(len + 1);
	if (ret == NULL)
		err(1, "malloc");
	len = 0;
	goto encode;
}

struct http_request *
http_get(const char *surl)
{
	struct url *url;
	struct http_request *req;
	struct hostent *he;
	struct sockaddr_in addr;
	size_t len, tlen;
	char ip_s[16];
#if TLS
	struct tls_config *tls_config;
	int tret;
#endif

	url = url_parse(surl);
	if (url == NULL)
		return NULL;

	req = malloc(sizeof(struct http_request));
	if (req == NULL)
		err(1, "malloc");
	memset(req, 0, sizeof(struct http_request));
	req->url = url;

	if (strcmp(url->scheme, "https") == 0) {
#if TLS
		req->https = 1;
#else
		errx(1, "requested HTTPS URL but no TLS support: %s", surl);
#endif
	}

	he = gethostbyname(req->url->host);
	if (he == NULL) {
		warnx("couldn't resolve host %s: %s", req->url->host,
		    hstrerror(h_errno));
		goto error;
	}

	req->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (req->socket == -1)
		err(1, "socket");

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(url->port);
	addr.sin_addr = *((struct in_addr *)he->h_addr);

	inet_ntop(AF_INET, &addr.sin_addr, ip_s, sizeof(ip_s));

#if DEBUG
	printf("connecting to %s (%s) %sto fetch %s\n",
	    req->url->host, ip_s, req->https ? "(with TLS) " : "",
	    req->url->path);
#endif

	if (connect(req->socket, (struct sockaddr *)&addr,
	    sizeof(addr)) == -1) {
		warn("failed connecting to %s (%s) port %d",
		  req->url->host, ip_s, req->url->port);
		goto error;
	}

#if TLS
	if (req->https) {
		tls_config = tls_config_new();
		if (tls_config == NULL)
			errx(1, "tls_config allocation failed");
		if (tls_config_set_protocols(tls_config,
		    TLS_PROTOCOLS_ALL) != 0)
			errx(1, "tls set protocols failed: %s",
			    tls_config_error(tls_config));
		if (tls_config_set_ciphers(tls_config, "legacy") != 0)
			errx(1, "tls set ciphers failed: %s",
			    tls_config_error(tls_config));

		req->tls = tls_client();
		if (req->tls == NULL)
			errx(1, "tls_client allocation failed");

		if (tls_configure(req->tls, tls_config) != 0)
			errx(1, "tls_configure failed: %s",
			    tls_config_error(tls_config));

		if (tls_connect_socket(req->tls, req->socket,
		    req->url->host) != 0) {
			warnx("TLS connect to %s failed: %s", req->url->host,
			    tls_error(req->tls));
			goto error;
		}

		do {
			tret = tls_handshake(req->tls);
		} while (tret == TLS_WANT_POLLIN || tret == TLS_WANT_POLLOUT);

		if (tret != 0) {
			warnx("TLS handshake to %s failed: %s",
			    req->url->host, tls_error(req->tls));
			goto error;
		}
	}
#endif

	tlen = 256 + strlen(req->url->host) + strlen(req->url->path);
	req->message = malloc(tlen);
	if (req->message == NULL)
		err(1, "malloc");

	len = snprintf(req->message, tlen,
	    "GET %s HTTP/1.0\r\n"
	    "Host: %s\r\n"
	    "User-Agent: %s\r\n"
	    "Accept: */*\r\n"
	    "\r\n",
	    req->url->path, req->url->host, __progname);
	if (len > tlen)
		errx(1, "snprintf overflow");

#if DEBUG
	printf(">>>[%zu] %s\n", len, req->message);
#endif

#if TLS
	if (req->https) {
		do {
			tlen = tls_write(req->tls, req->message, len);
		} while (tlen == TLS_WANT_POLLIN || tlen == TLS_WANT_POLLOUT);
	} else
#endif
	{
		tlen = write(req->socket, req->message, len);
	}
	if (tlen != len)
		err(1, "short write");

	return req;

error:
	http_req_free(req);
	return NULL;
}

ssize_t
http_req_read(struct http_request *req, char *data, size_t len)
{
	fd_set fds;
	struct timeval timeout;
	ssize_t ret;

	if (!req || !req->socket)
		return -1;

	FD_ZERO(&fds);
	FD_SET(req->socket, &fds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	switch (select(req->socket + 1, &fds, NULL, NULL, &timeout)) {
	case -1:
		err(1, "select");
	case 0:
		return 0;
	}

#if TLS
	if (req->https) {
		do {
			ret = tls_read(req->tls, data, len);
		} while (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT);
	} else
#endif
	{
		ret = read(req->socket, data, len);
	}
#if DEBUG
	printf("<<<[%zu] %s\n", len, data);
#endif

	if (ret == -1) {
#if TLS
		if (req->https) {
			tls_close(req->tls);
			tls_free(req->tls);
			req->tls = NULL;
			req->https = 0;
		}
#endif
		close(req->socket);
		req->socket = 0;
		return -1;
	}
	return ret;
}

int
http_req_skip_header(struct http_request *req)
{
	size_t len, n;

	for (;;) {
		if (req->chunk_len > 3) {
			/*
			 * Leave last 3 bytes of previous read in case \r\n\r\n
			 * happens across reads.
			 */
			memmove(req->chunk, req->chunk + req->chunk_len - 3,
			  req->chunk_len - 3);
			req->chunk_len = 3;
		}
		len = http_req_read(req, req->chunk + req->chunk_len,
		  sizeof(req->chunk) - req->chunk_len);
		if (len < 0)
			return 0;
		if (len == 0)
			continue;
		req->chunk_len += len;

		for (n = 3; n < req->chunk_len; n++) {
			if (req->chunk[n - 3] != '\r' ||
			    req->chunk[n - 2] != '\n' ||
			    req->chunk[n - 1] != '\r' ||
			    req->chunk[n] != '\n')
				continue;

			req->chunk_len -= n + 1;
			memmove(req->chunk, req->chunk + n + 1, req->chunk_len);
			req->chunk_off = 0;
			return 1;
		}
	}

	return 0;
}

char *
http_req_chunk_peek(struct http_request *req)
{
	if (req->chunk_len == 0 || (req->chunk_off + 1 > req->chunk_len)) {
		req->chunk_len = http_req_read(req, req->chunk,
		    sizeof(req->chunk));
		if (req->chunk_len < 0)
			return NULL;
		req->chunk_off = 0;
	}

	if (req->chunk_len == 0 || (req->chunk_off + 1 > req->chunk_len))
		return NULL;

	return req->chunk + req->chunk_off;
}

char *
http_req_chunk_read(struct http_request *req)
{
	char *chunk;

	chunk = http_req_chunk_peek(req);
	if (chunk == NULL)
		return NULL;

	req->chunk_off = req->chunk_len;
	return chunk;
}

char
http_req_byte_peek(struct http_request *req)
{
	if (req->chunk_len == 0 || (req->chunk_off + 1 > req->chunk_len)) {
		req->chunk_len = http_req_read(req, req->chunk,
		    sizeof(req->chunk));
		if (req->chunk_len < 0)
			return 0;
		req->chunk_off = 0;
	}

	if (req->chunk_len == 0 || (req->chunk_off + 1 > req->chunk_len))
		return 0;

	return req->chunk[req->chunk_off];
}

char
http_req_byte_read(struct http_request *req)
{
	char c;

	c = http_req_byte_peek(req);
	if (c == 0)
		return 0;

	req->chunk_off++;
	return c;
}

void
http_req_free(struct http_request *req)
{
	if (req == NULL)
		return;

#if TLS
	if (req->https && req->tls) {
		tls_close(req->tls);
		tls_free(req->tls);
		req->tls = NULL;
	}
#endif
	if (req->socket)
		close(req->socket);
	if (req->message != NULL)
		free(req->message);
	if (req->url)
		free(req->url);
}
