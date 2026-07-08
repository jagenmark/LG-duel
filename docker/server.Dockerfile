# Make server docker image
FROM ubuntu:24.04


RUN apt-get update && apt-get install -y \
  cmake \
  ninja-build \
  build-essential

WORKDIR /app

COPY CMakePresets.json CMakeLists.txt .
COPY src/ src/
COPY cmake/ cmake/
COPY tests/ tests/
COPY tools/ServerProbe.cpp tools/ServerProbe.cpp
COPY tools/MapValidate.cpp tools/MapValidate.cpp
RUN mkdir -p build/default

#RUN cmake --preset default
RUN cmake --build --preset default --target lg_duel_server
