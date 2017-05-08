LIBS = -ljack -lshout -lvorbis -lvorbisenc -logg -lopus
CFLAGS = -std=gnu99 -g

OBJECTS = \
	tidstream.o \
	audio.o \
	circbuf.o \
	stream.o \
	enc_vorbis.o \
	enc_opus.o \
	opus_header.o

tidstream: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

install: tidstream
	install -m 755 tidstream /usr/bin/tidstream

.PHONY: clean
clean:
	rm -f *.o tidstream

