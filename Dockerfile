# docker build -t codilime_recruitment/shared_ptr:latest .

FROM ubuntu:20.04

# Disable interactive dialogues in debconf/apt
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -q \
 && apt-get install -q -y --no-install-recommends \
    clang-10 \
    cmake \
    make \
    libstdc++-10-dev \
 && apt-get clean -q

ENV CC=/usr/bin/clang-10
ENV CXX=/usr/bin/clang++-10

COPY ["./", "/usr/src/cpp/codilime/"]

WORKDIR /usr/src/cpp/codilime/


RUN mkdir build && \
    cd build && \
    cmake  .. && \
    make -j 2 all && \
    ./tests/tests
