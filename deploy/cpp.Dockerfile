# syntax=docker/dockerfile:1.7
# Multi-stage build for the four C++ services. One builder stage compiles and tests everything once;
# each runtime target copies a single binary onto a minimal Ubuntu base.
#   docker build -f deploy/cpp.Dockerfile --target identity-service -t socialgraph/identity .

ARG UBUNTU=ubuntu:24.04

FROM ${UBUNTU} AS builder
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ cmake ninja-build pkg-config \
      libdrogon-dev libjsoncpp-dev libcpp-httplib-dev nlohmann-json3-dev libpqxx-dev libpq-dev \
      libsodium-dev libhiredis-dev libgtest-dev libbenchmark-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake cmake
COPY libs libs
COPY services services
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSG_BUILD_BENCHMARKS=OFF \
      -DSG_WARNINGS_AS_ERRORS=ON \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure \
    && cmake --install build --prefix /opt/socialgraph

FROM ${UBUNTU} AS runtime
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      libdrogon1t64 libtrantor1 libjsoncpp25 libcpp-httplib0.14t64 libpqxx-7.8t64 libpq5 libsodium23 \
      libhiredis1.1.0 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --no-create-home socialgraph
USER 10001
ENTRYPOINT []

FROM runtime AS identity-service
COPY --from=builder /opt/socialgraph/bin/identity-service /usr/local/bin/identity-service
COPY data/stackoverflow /data/stackoverflow
EXPOSE 8081
HEALTHCHECK --interval=10s --timeout=3s --start-period=10s --retries=3 CMD ["identity-service", "healthcheck"]
CMD ["identity-service", "serve"]

FROM runtime AS graph-service
COPY --from=builder /opt/socialgraph/bin/graph-service /usr/local/bin/graph-service
COPY data/stackoverflow /data/stackoverflow
EXPOSE 8082
HEALTHCHECK --interval=10s --timeout=3s --start-period=10s --retries=3 CMD ["graph-service", "healthcheck"]
CMD ["graph-service", "serve"]

FROM runtime AS analytics-service
COPY --from=builder /opt/socialgraph/bin/analytics-service /usr/local/bin/analytics-service
EXPOSE 8083
HEALTHCHECK --interval=10s --timeout=3s --start-period=10s --retries=3 CMD ["analytics-service", "healthcheck"]
CMD ["analytics-service", "serve"]

FROM runtime AS content-service
COPY --from=builder /opt/socialgraph/bin/content-service /usr/local/bin/content-service
COPY data/stackoverflow /data/stackoverflow
EXPOSE 8084
HEALTHCHECK --interval=10s --timeout=3s --start-period=60s --retries=3 CMD ["content-service", "healthcheck"]
CMD ["content-service", "serve"]
