FROM gcc:latest

# ZMQ C library + nlohmann/json; also fetch the single header zmq.hpp
RUN apt-get update && apt-get install -y --no-install-recommends \
      libzmq3-dev nlohmann-json3-dev ca-certificates wget \
    && update-ca-certificates \
    && wget -O /usr/local/include/zmq.hpp https://raw.githubusercontent.com/zeromq/cppzmq/v4.10.0/zmq.hpp \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY sensor.cpp /app/

# Compile with libzmq (C API) — zmq.hpp is header-only
RUN g++ -std=gnu++17 -O2 sensor.cpp -o sensor -lzmq -pthread

CMD ["./sensor"]
