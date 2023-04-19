/* vim:ts=8
 *
 * Copyright (c) 2023 joshua stein <jcs@jcs.org>
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
#include <ctype.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/types.h>

#if TLS
#include <tls.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/xpm.h>

#include "http.h"
#include "pdjson.h"

#include "icons/clouds.xpm"
#include "icons/moon.xpm"
#include "icons/rain.xpm"
#include "icons/snow.xpm"
#include "icons/sun.xpm"

struct {
	Display *dpy;
	int screen;
	Window win;
	XWMHints hints;
	GC gc;
} xinfo = { 0 };

enum icon_type {
	ICON_SUN,
	ICON_CLOUDS,
	ICON_MOON,
	ICON_RAIN,
	ICON_SNOW,
};

struct icon_map_entry {
	char **xpm;
	enum icon_type value;
	Pixmap pm;
	Pixmap pm_mask;
	XpmAttributes pm_attrs;
} icon_map[] = {
	{ sun_xpm, ICON_SUN },
	{ clouds_xpm, ICON_CLOUDS },
	{ moon_xpm, ICON_MOON },
	{ rain_xpm, ICON_RAIN },
	{ snow_xpm, ICON_SNOW },
};

extern char *__progname;

void	killer(int);
void	usage(void);
void	redraw_icon(void);
int	fetch_weather_read(void *cookie);
int	fetch_weather_peek(void *cookie);
int	fetch_weather(void);

int	exit_msg[2];
int	weather_check_secs = (60 * 30);
struct timespec last_weather_check;

char	*api_key = NULL;
char	*zipcode = NULL;
int	fahrenheit = 1;

char	current_conditions[100];
double	current_temp;
enum icon_type current_condition_icon;

#define WINDOW_WIDTH		200
#define WINDOW_HEIGHT		100

int
main(int argc, char* argv[])
{
	XEvent event;
	XSizeHints *hints;
	XGCValues gcv;
	struct pollfd pfd[2];
	struct sigaction act;
	struct timespec now, delta;
	char *display = NULL;
	long sleep_secs;
	int ch, i;

	while ((ch = getopt(argc, argv, "cd:i:k:z:")) != -1) {
		switch (ch) {
		case 'c':
			fahrenheit = 0;
			break;
		case 'd':
			display = optarg;
			break;
		case 'i':
			weather_check_secs = atoi(optarg);
			if (weather_check_secs < 1)
				errx(1, "interval must be >= 1");
			break;
		case 'k':
			api_key = strdup(optarg);
			break;
		case 'z':
			zipcode = strdup(optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (api_key == NULL)
		errx(1, "must supply openweathermap.org API key with -k");
	if (zipcode == NULL)
		errx(1, "must supply zipcode with -z");

	if (!(xinfo.dpy = XOpenDisplay(display)))
		errx(1, "can't open display %s", XDisplayName(display));

#ifdef __OpenBSD_
	if (pledge("stdio dns inet") == -1)
		err(1, "pledge");
#endif

	/* setup exit handler pipe that we'll poll on */
	if (pipe2(exit_msg, O_CLOEXEC) != 0)
		err(1, "pipe2");
	act.sa_handler = killer;
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	xinfo.screen = DefaultScreen(xinfo.dpy);
	xinfo.win = XCreateSimpleWindow(xinfo.dpy,
	    RootWindow(xinfo.dpy, xinfo.screen),
	    0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
	    BlackPixel(xinfo.dpy, xinfo.screen),
	    WhitePixel(xinfo.dpy, xinfo.screen));
	gcv.foreground = 1;
	gcv.background = 0;
	xinfo.gc = XCreateGC(xinfo.dpy, xinfo.win, GCForeground | GCBackground,
	    &gcv);
	XSetFunction(xinfo.dpy, xinfo.gc, GXcopy);

	/* load XPMs */
	for (i = 0; i < sizeof(icon_map) / sizeof(icon_map[0]); i++) {
		if (XpmCreatePixmapFromData(xinfo.dpy,
		    RootWindow(xinfo.dpy, xinfo.screen),
		    icon_map[i].xpm, &icon_map[i].pm,
		    &icon_map[i].pm_mask, &icon_map[i].pm_attrs) != 0)
			errx(1, "XpmCreatePixmapFromData failed");
	}

	hints = XAllocSizeHints();
	if (!hints)
		err(1, "XAllocSizeHints");
	hints->flags = PMinSize | PMaxSize;
	hints->min_width = WINDOW_WIDTH;
	hints->min_height = WINDOW_HEIGHT;
	hints->max_width = WINDOW_WIDTH;
	hints->max_height = WINDOW_HEIGHT;
#if 0	/* disabled until progman displays minimize on non-dialog wins */
	XSetWMNormalHints(xinfo.dpy, xinfo.win, hints);
#endif

	fetch_weather();

	xinfo.hints.initial_state = IconicState;
	xinfo.hints.flags |= StateHint;
	XSetWMHints(xinfo.dpy, xinfo.win, &xinfo.hints);
	XMapWindow(xinfo.dpy, xinfo.win);

	memset(&pfd, 0, sizeof(pfd));
	pfd[0].fd = ConnectionNumber(xinfo.dpy);
	pfd[0].events = POLLIN;
	pfd[1].fd = exit_msg[0];
	pfd[1].events = POLLIN;

	/* we need to know when we're exposed */
	XSelectInput(xinfo.dpy, xinfo.win, ExposureMask);

	for (;;) {
		if (!XPending(xinfo.dpy)) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			timespecsub(&now, &last_weather_check, &delta);

			if (delta.tv_sec > weather_check_secs)
				sleep_secs = 0;
			else
				sleep_secs = ((long)weather_check_secs -
				    delta.tv_sec);

			poll(pfd, 2, sleep_secs * 1000);
			if (pfd[1].revents)
				/* exit msg */
				break;

			if (!XPending(xinfo.dpy)) {
				clock_gettime(CLOCK_MONOTONIC, &now);
				timespecsub(&now, &last_weather_check, &delta);
				if (delta.tv_sec >= weather_check_secs)
					fetch_weather();
				else
					redraw_icon();
				continue;
			}
		}

		XNextEvent(xinfo.dpy, &event);

		switch (event.type) {
		case Expose:
			redraw_icon();
			break;
		}
	}

	for (i = 0; i < sizeof(icon_map) / sizeof(icon_map[0]); i++) {
		if (icon_map[i].pm)
			XFreePixmap(xinfo.dpy, icon_map[i].pm);
		if (icon_map[i].pm_mask)
			XFreePixmap(xinfo.dpy, icon_map[i].pm_mask);
	}

	XDestroyWindow(xinfo.dpy, xinfo.win);
	XFree(hints);
	XCloseDisplay(xinfo.dpy);

	return 0;
}

void
killer(int sig)
{
	if (write(exit_msg[1], &exit_msg, 1))
		return;

	warn("failed to exit cleanly");
	exit(1);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s %s\n", __progname,
		"-k api_key -z zipcode [-c] [-d display] [-i interval]");
	exit(1);
}

int
fetch_weather_read(void *cookie)
{
	struct http_request *req = (struct http_request *)cookie;

	return (int)http_req_byte_read(req);
}

int
fetch_weather_peek(void *cookie)
{
	struct http_request *req = (struct http_request *)cookie;

	return (int)http_req_byte_peek(req);
}

int
fetch_weather(void)
{
	static char *url = NULL;
	struct http_request *req;
	json_stream js;
	enum json_type jt;
	const char *str;
	int weather_id, night;
	enum {
		STATE_BEGIN,
		STATE_IN_WEATHER,
		STATE_IN_WEATHER_ID,
		STATE_IN_WEATHER_DESC,
		STATE_IN_WEATHER_ICON,
		STATE_IN_MAIN,
		STATE_IN_MAIN_TEMP,
	} state = STATE_BEGIN;

	clock_gettime(CLOCK_MONOTONIC, &last_weather_check);

	if (url == NULL) {
		url = malloc(256);
		if (url == NULL)
			err(1, "malloc");

		snprintf(url, 256, "%s://api.openweathermap.org/data/2.5/"
		    "weather?zip=%s&appid=%s&units=%s&mode=json",
#if TLS
		    "https",
#else
		    "http",
#endif
		    zipcode, api_key, fahrenheit ? "imperial" : "metric");
	}

	req = http_get(url);
	if (req == NULL)
		return 1;

	if (http_req_skip_header(req) != 1) {
		warnx("failed reading HTTP body");
		http_req_free(req);
		return 1;
	}

	snprintf(current_conditions, sizeof(current_conditions),
	    "(Failed to parse API response)");
	current_temp = 0;
	weather_id = 0;
	night = 0;

	/* https://openweathermap.org/current#parameter */
	json_open_user(&js, fetch_weather_read, fetch_weather_peek, req);
	for (; jt = json_next(&js), jt != JSON_DONE && !json_get_error(&js);) {
		if (jt == JSON_STRING)
			str = json_get_string(&js, 0);

#if DEBUG
		printf("[%d] jt %d %s\n", state, jt,
		    jt == JSON_STRING ? str : "");
#endif

		switch (state) {
		case STATE_BEGIN:
			if (jt == JSON_STRING && strcmp(str, "weather") == 0)
				state = STATE_IN_WEATHER;
			else if (jt == JSON_STRING && strcmp(str, "main") == 0)
				state = STATE_IN_MAIN;
			break;
		case STATE_IN_WEATHER:
			if (jt == JSON_STRING &&
			    strcmp(str, "description") == 0)
				state = STATE_IN_WEATHER_DESC;
			else if (jt == JSON_STRING && strcmp(str, "id") == 0)
				state = STATE_IN_WEATHER_ID;
			else if (jt == JSON_STRING && strcmp(str, "icon") == 0)
				state = STATE_IN_WEATHER_ICON;
			else if (jt == JSON_OBJECT_END)
				state = STATE_BEGIN;
			break;
		case STATE_IN_WEATHER_ID:
			if (jt == JSON_NUMBER)
				weather_id = json_get_number(&js);
			state = STATE_IN_WEATHER;
			break;
		case STATE_IN_WEATHER_ICON:
			if (jt == JSON_STRING)
				/* "13d" or "04n" */
				night = (str[2] == 'n');
			state = STATE_IN_WEATHER;
			break;
		case STATE_IN_WEATHER_DESC:
			strlcpy(current_conditions, str,
			    sizeof(current_conditions));
			current_conditions[0] = toupper(current_conditions[0]);
			state = STATE_IN_WEATHER;
			break;
		case STATE_IN_MAIN:
			if (jt == JSON_STRING && strcmp(str, "temp") == 0)
				state = STATE_IN_MAIN_TEMP;
			break;
		case STATE_IN_MAIN_TEMP:
			if (jt == JSON_NUMBER)
				current_temp = json_get_number(&js);
			state = STATE_IN_MAIN;
			break;
		}
	}

	http_req_free(req);

#if DEBUG
	printf("current conditions: %s\ntemperature: %d\nweather_id: %d\n",
	    current_conditions, (int)current_temp, weather_id);
#endif

	snprintf(current_conditions + strlen(current_conditions),
	    sizeof(current_conditions) - strlen(current_conditions),
	    ", %d%c%c", (int)current_temp, 0xb0, /* degrees symbol */
	    fahrenheit ? 'F' : 'C');

	/* https://openweathermap.org/weather-conditions */
	if (weather_id >= 200 && weather_id <= 399)
		current_condition_icon = ICON_RAIN;
	else if (weather_id >= 500 && weather_id <= 599)
		current_condition_icon = ICON_RAIN;
	else if (weather_id >= 600 && weather_id <= 699)
		current_condition_icon = ICON_SNOW;
	else if (weather_id >= 801 && weather_id <= 804)
		current_condition_icon = ICON_CLOUDS;
	else {
		if (night)
			current_condition_icon = ICON_MOON;
		else
			current_condition_icon = ICON_SUN;
	}

	redraw_icon();

	return 0;
}

void
redraw_icon(void)
{
	XTextProperty title_prop;
	XWindowAttributes xgwa;
	char *titlep = (char *)&current_conditions;
	int i, rc, xo = 0, yo = 0, icon = 0;

	for (i = 0; i < sizeof(icon_map) / sizeof(icon_map[0]); i++) {
		if (icon_map[i].value == current_condition_icon) {
			icon = i;
			break;
		}
	}

	/* update icon and window titles */
	if (!(rc = XStringListToTextProperty(&titlep, 1, &title_prop)))
		errx(1, "XStringListToTextProperty");
	XSetWMIconName(xinfo.dpy, xinfo.win, &title_prop);
	XStoreName(xinfo.dpy, xinfo.win, current_conditions);

	xinfo.hints.icon_pixmap = icon_map[icon].pm;
	xinfo.hints.icon_mask = icon_map[icon].pm_mask;
	xinfo.hints.flags = IconPixmapHint | IconMaskHint;
	XSetWMHints(xinfo.dpy, xinfo.win, &xinfo.hints);

	/* and draw it in the center of the window */
	XGetWindowAttributes(xinfo.dpy, xinfo.win, &xgwa);
	xo = (xgwa.width / 2) - (icon_map[icon].pm_attrs.width / 2);
	yo = (xgwa.height / 2) - (icon_map[icon].pm_attrs.height / 2);
	XSetClipMask(xinfo.dpy, xinfo.gc, icon_map[icon].pm_mask);
	XSetClipOrigin(xinfo.dpy, xinfo.gc, xo, yo);
	XClearWindow(xinfo.dpy, xinfo.win);
	XSetFunction(xinfo.dpy, xinfo.gc, GXcopy);
	XCopyArea(xinfo.dpy, icon_map[icon].pm,
	    xinfo.win, xinfo.gc,
	    0, 0,
	    icon_map[icon].pm_attrs.width, icon_map[icon].pm_attrs.height,
	    xo, yo);
}
