# xweathericon

A small weather widget for X11 which is intended to be iconified all the time
to remain on the desktop.
Its icon and window title indicate the current weather as fetched from the
[OpenWeatherMap API](https://openweathermap.org/).

It was written to work with
[progman](https://github.com/jcs/progman)
but should work with any X11 window manager that handles `IconicState`
hints and shows icons in a useful manner.

## License

ISC

## Dependencies

`libX11` and `libXpm`, optionally `libtls` from LibreSSL

## Compiling

Fetch the source, `make` and then `make install`

## Usage

You must obtain a free API key from
[OpenWeatherMap](https://openweathermap.org/)
and supply it as the `-k` parameter, along with the `-z` parameter containing
your local zip code.
