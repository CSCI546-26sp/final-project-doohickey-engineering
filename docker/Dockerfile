FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake git pkg-config \
    autoconf libtool curl ca-certificates \
    zlib1g-dev libssl-dev dos2unix \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# --- copy ONLY CMakeLists.txt first and fetch+compile deps ---
# this layer is cached as long as CMakeLists.txt doesn't change
COPY CMakeLists.txt .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
    && cmake --build build --target Catch2 -j$(nproc) \
    || true
RUN cmake --build build --target grpc++ -j$(nproc) || true

# --- now copy your actual source and build ---
COPY . .
RUN find . -type f \( \
    -name "*.cpp" -o -name "*.h" -o \
    -name "*.sh"  -o -name "*.proto" -o \
    -name "CMakeLists.txt" \
    \) | xargs dos2unix 2>/dev/null || true

RUN chmod +x docker/scripts/run_tests.sh

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
RUN cmake --build build -j$(nproc)

CMD ["bash", "docker/scripts/run_tests.sh"]