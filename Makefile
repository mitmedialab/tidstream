LIBS = -ljack -lshout -lvorbis -lvorbisenc -logg -lopus
CFLAGS = -std=gnu99 -g

TARGETS = tidstream opusplit

tidstream_OBJECTS = \
	tidstream.o \
	audio.o \
	circbuf.o \
	stream.o \
	enc_vorbis.o \
	enc_opus.o \
	opus_header.o

opusplit_OBJECTS = \
	opusplit.o \
	opus_header.o \
	opus_utils.o \
	file_writer.o

all: $(TARGETS)

tidstream: $(tidstream_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

opusplit: $(opusplit_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

install: tidstream
	install -m 755 tidstream /usr/bin/tidstream

.PHONY: clean
clean:
	rm -f *.o $(TARGETS)

