VERSION ?= 0.0.0
PROGRAM_NAME = vinyl-edit
OUTPUT = $(PROGRAM_NAME)_$(VERSION)
VINYL_SRC = lib/vinyl-cache
LIBVCC = $(VINYL_SRC)/lib/libvcc/.libs/libvcc.a
LIBVARNISH = $(VINYL_SRC)/lib/libvarnish/.libs/libvarnish.a

.PHONY: clean nuke test libvcc.a dist dist-darwin-arm64 dist-linux-arm64 dist-linux-amd64

INCLUDES = \
	-I$(VINYL_SRC)/include \
	-I$(VINYL_SRC)/lib/libvcc \
	-I$(VINYL_SRC)

EXTRA_LIBS ?=

LIBS = \
	$(LIBVCC) \
	$(LIBVARNISH) \
	$(shell pkg-config --libs libpcre2-8) \
	-lm \
	$(EXTRA_LIBS)

SRCS = src/main.c src/edit.c src/buf.c src/pattern.c src/format.c

build: $(SRCS) $(LIBVCC) $(LIBVARNISH)
	@mkdir -p dist
	cc -Wall -O2 -D VINYL_EDIT_VERSION=\"$(VERSION)\" $(INCLUDES) -o dist/$(OUTPUT) $(SRCS) $(LIBS)

libvcc.a:
	git submodule update --init --recursive
	cd $(VINYL_SRC) && ./autogen.sh && ./configure && make -C include vcs_version.h && make -C lib/libvarnish && make -C lib/libvcc

$(LIBVCC) $(LIBVARNISH):
	$(MAKE) libvcc.a

dist-darwin-arm64: build
	cp dist/$(OUTPUT) dist/$(OUTPUT)_darwin_arm64

dist-linux-arm64:
	docker buildx build --platform linux/arm64 --build-arg VERSION=$(VERSION) --build-arg PLATFORM=linux_arm64 -o dist/ .

dist-linux-amd64:
	docker buildx build --platform linux/amd64 --build-arg VERSION=$(VERSION) --build-arg PLATFORM=linux_amd64 -o dist/ .

dist: build dist-linux-arm64 dist-linux-amd64 dist-darwin-arm64

test:
	@BINARY=./dist/$(OUTPUT) ./tools/run-tests.sh

clean:
	rm -rf dist

nuke: clean
	rm -rf lib
