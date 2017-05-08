# tidstream

This project contains utilities for creating and manipulating multi-channel
Ogg Vorbis and Ogg Opus streams, originally for the Tidmarsh project but
generally useful for other purposes.

These tools were created since nearly all existing software for generating and
manipulating audio streams is limited to mono or stereo data, or makes
incorrect assumptions about surround sound channel layouts.

## Installation

A basic Makefile is provided, which should build the software on Linux systems
with reasonably standard paths.  OS X users may need to tweak library and
include paths.

## tidstream

The primary tool is a SHOUTcast client called `tidstream`.  It receives audio
input from JACK, encodes it as Ogg Vorbis or Ogg Opus, and pushes it to an
Icecast server.  Arbitrary numbers of channels are supported.

### Usage

`-A`

> automatically connects input channels to the JACK system inputs,
> sequentially starting with the channel specified (see `-O`).

`-O <offset>`

> specifies the number of the first input channel to be connected.

`-r`

> automatically retry when an error is encountered (usually network-related)

`-c <channels>`

> number of channels to encode

`-h <hostname>`

> hostname of the Icecast server

`-p <port>`

> port of the Icecast server

`-u <mountpoint>`

> mountpoint to use on the Icecast server

`-w <password>`

> password to use for authenticating to the Icecast server

`-m <min bitrate>`

> minimum bitrate for the VBR encoder, in kbps

`-a <average bitrate>`

> average bitrate for the VBR encoder, in kbps

`-x <max bitrate>`

> maximum bitrate for the VBR encoder, in kbps

`-o`

> use Opus as the codec; if not specified, Vorbis is used
