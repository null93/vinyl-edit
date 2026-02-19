FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    autoconf automake libtool pkg-config gcc make git \
    libpcre2-dev libedit-dev libbsd-dev python3-docutils python3-sphinx \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY . .

# Initialize a git repo so vinyl autogen.sh works
RUN git init && git add -A && \
    git -c user.name="build" -c user.email="build@build" commit -m "build"

# Build vinyl libraries
RUN cd lib/vinyl-cache && \
    ./autogen.sh && \
    ./configure && \
    make -C include vcs_version.h && \
    make -C lib/libvarnish && \
    make -C lib/libvcc

# Build vinyl-edit (libbsd needed for strlcpy on Linux)
ARG VERSION=0.0.0
RUN make build EXTRA_LIBS=-lbsd VERSION=${VERSION}

FROM scratch
ARG VERSION=0.0.0
ARG PLATFORM=linux_arm64
COPY --from=builder /src/dist/vinyl-edit_${VERSION} /vinyl-edit_${VERSION}_${PLATFORM}
