# syntax=docker/dockerfile:1

# ---------- build stage ----------
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc \
        make \
        libncurses-dev \
        libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Only the pieces needed to build the demo server.
COPY makefile ./
COPY include/ ./include/
COPY src/     ./src/
COPY demo/    ./demo/

# Build bin/http_server via the project's own rule.
# OPFLAGS override drops -march=native (and the test-only -IUnity/src include)
# so the binary runs on any x86-64 host, not just this build machine's CPU.
RUN make demo OPFLAGS="-std=c11 -Wall -Wextra -Iinclude -O3 -ffast-math -pthread"

# ---------- runtime stage ----------
FROM debian:bookworm-slim AS runtime

# Server links libncursesw for the (TTY-only) dashboard; needed at runtime even
# though the container runs headless.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libncursesw6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --uid 10001 appuser

WORKDIR /app
COPY --from=build /src/bin/http_server /app/http_server
COPY --from=build /src/demo/www        /app/www

USER appuser
EXPOSE 8080

# The server only installs a SIGINT handler for graceful shutdown; `docker stop`
# / `podman stop` send SIGTERM by default and would wait 10s then SIGKILL it
# mid-request. Make stop send SIGINT so it drains and exits cleanly.
STOPSIGNAL SIGINT

# --host 0.0.0.0  : default is 127.0.0.1, which is unreachable from outside the
#                   container; bind all interfaces instead.
# --docroot /app/www : default docroot is the relative path "demo/www".
# --no-monitor    : explicit; the ncurses dashboard only starts on a TTY anyway.
# Add "--workers N" to the end if you want to pin the pool size.
CMD ["/app/http_server", "--host", "0.0.0.0", "--port", "8080", \
     "--docroot", "/app/www", "--no-monitor"]
