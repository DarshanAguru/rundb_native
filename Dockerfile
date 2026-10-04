# ==========================================
# RunDB Multi-Stage Production Containerfile
# Packaging: Hermetic build with Quill, Zlib, and Jemalloc
# ==========================================

# ------------------------------------------
# Stage 1: Build & Dependency Resolution
# ------------------------------------------
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install core build toolchain and native zlib
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
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Install and bootstrap vcpkg for hermetic dependency management
ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git /opt/vcpkg && \
    /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /app

# Copy dependency definition first for Docker layer caching
COPY vcpkg.json ./

# Pre-install dependencies defined in vcpkg.json (Quill asynchronous logging engine)
RUN vcpkg install --triplet x64-linux

# Copy complete project source code including bundled lib/libjemalloc.so
COPY . .

# Configure and compile Release binary with LTO and optimizations
RUN cmake --preset release && \
    cmake --build --preset build-release

# ------------------------------------------
# Stage 2: Minimal Production Runtime Image
# ------------------------------------------
FROM ubuntu:24.04 AS runner

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies (Zlib shared library, certificates)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Security: Create unprivileged system user and group
RUN groupadd -g 10001 rundb && \
    useradd -u 10001 -g rundb -s /sbin/nologin -M rundb

WORKDIR /app

# Copy compiled binaries and bundled jemalloc from builder stage
COPY --from=builder /app/build/release/rundb /app/rundb
COPY --from=builder /app/build/release/rundb_tests /app/rundb_tests
COPY --from=builder /app/build/release/rundb_benchmark /app/rundb_benchmark
COPY --from=builder /app/build/release/libjemalloc.so /app/libjemalloc.so
COPY --from=builder /app/build/release/libjemalloc.so /app/lib/libjemalloc.so
COPY --from=builder /app/config /app/config

# Ensure dynamic linker resolves jemalloc
ENV LD_LIBRARY_PATH="/app:/app/lib:${LD_LIBRARY_PATH}"

# Prepare persistent data volume directory with proper permissions
RUN mkdir -p /data && chown -R rundb:rundb /app /data

USER rundb:rundb

WORKDIR /data

# Default RunDB port
EXPOSE 7379

VOLUME ["/data"]

# Default command: Bind to all interfaces, persist in /data
ENTRYPOINT ["/app/rundb"]
CMD ["--host", "0.0.0.0", "--port", "7379", "--aof-file", "/data/run_master.aof", "--snapshot-file", "/data/dump.rdb"]
