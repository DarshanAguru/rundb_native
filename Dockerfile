# ==========================================
# Build Stage
# ==========================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    curl \
    pkg-config \
    zip \
    unzip \
    tar \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy dependency definition for layer caching
COPY vcpkg.json ./

# Copy complete project source
COPY . .

# Configure and compile Release binary
RUN cmake --preset release && \
    cmake --build --preset build-release

# ==========================================
# Production Runtime Stage
# ==========================================
FROM ubuntu:24.04 AS runner

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Security: Create unprivileged system user
RUN groupadd -g 10001 rundb && \
    useradd -u 10001 -g rundb -s /sbin/nologin -M rundb

WORKDIR /app

# Copy binary and bundled jemalloc from builder
COPY --from=builder /app/build/release/rundb /app/rundb
COPY --from=builder /app/build/release/libjemalloc.so /app/libjemalloc.so
COPY --from=builder /app/config /app/config

# Create data directory for AOF persistence
RUN mkdir -p /data && chown -R rundb:rundb /app /data

USER rundb:rundb

EXPOSE 7379

VOLUME ["/data"]

ENTRYPOINT ["/app/rundb", "--host", "0.0.0.0", "--port", "7379"]
