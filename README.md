# jack2libstlseries

Simple [libstlseries](https://github.com/Spekadyon/libstlseries/) front-end
using audio input to modulate the keyboard color.


## Description

jack2libstlseries is a [jack](http://jackaudio.org) client using audio input to
illuminate SteelSeries keyboards supported by libstlseries. Each of the three
regions of the keyboard corresponds to a range of audio frequencies. The left
part is assigned to bass, the middle one to medium and the right one to treble.

### Implementation details

Fourier transform of audio samples are calculated with FFTW. A color is assigned
to the amplitude of the signal, one for region of frequency. Keyboard color is
set using libstlseries.


## Dependencies

* [FFTW](http://www.fftw.org/) v3
* [JACK](http://jackaudio.org)
* [libstlseries](https://github.com/Spekadyon/libstlseries/)


## Build

To build the library (on Linux):

1. If needed, edit Makefile and jack2libstlseries.h
2. make

No installation facilities are provided. You are free to copy the binary
wherever you want, if needed.


## Usage

1. launch jack server
2. launch jack2libstlseries
3. connect jack2libstlseries input to the output you want, using
   [qjackctl](http://qjackctl.sourceforge.net/) or
   [njconnect](http://sourceforge.net/projects/njconnect/) for example.

