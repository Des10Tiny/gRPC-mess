FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    protobuf-compiler-grpc libgrpc++-dev libprotobuf-dev \
    nlohmann-json3-dev libabsl-dev

WORKDIR /app
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release &&\
    cmake --build build --config Release --target messenger_client --parallel


FROM ubuntu:22.04 AS runner
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libgrpc++1 libprotobuf23 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/messenger_client /app/messenger_client

CMD ["/app/messenger_client"]