# Builder image (See https://hub.docker.com/layers/library/ubuntu/26.04/images/sha256-157dc3d6e4a3e4f2c68d12df9df9ed6e808b59af6d4c3757b432b4f519916725)
FROM docker.io/ubuntu:26.04@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c AS builder

# Install dependencies
RUN DEBIAN_FRONTEND=noninteractive \
  apt update && \
  apt install --no-install-recommends --yes \
  'libconfig++-dev' \
  build-essential \
  cmake \
  git \
  libfftw3-dev \
  libmp3lame-dev \
  libshout3-dev \
  libsoapysdr-dev \
  pkg-config && \
  rm -rf /var/lib/apt/lists/*

# Build the application
WORKDIR /app
COPY . /app
RUN cmake -DPLATFORM=generic -DCMAKE_BUILD_TYPE=Release -DNFM=TRUE -S /app -B /app/build && \
  cmake --build /app/build -j$(nproc)

# Runtime image (See https://hub.docker.com/layers/library/ubuntu/26.04/images/sha256-157dc3d6e4a3e4f2c68d12df9df9ed6e808b59af6d4c3757b432b4f519916725)
FROM docker.io/ubuntu:26.04@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c
ARG GID=1000
ARG UID=1000

# Copy the application
WORKDIR /app
COPY --from=builder /app/build/src/rtl_airband /app/rtl_airband
RUN chmod 755 /app/rtl_airband

# Install dependencies
RUN DEBIAN_FRONTEND=noninteractive \
  apt update && \
  apt install --no-install-recommends --yes \
  'libconfig++-dev' \
  gettext-base \
  libfftw3-dev \
  libmp3lame-dev \
  libshout3-dev \
  soapysdr-tools \
  soapysdr0.8-module-all \
  usbutils && \
  rm -rf /var/lib/apt/lists/*

# Create the entrypoint
RUN echo '#!/usr/bin/env bash\n\
  set -euo pipefail\n\
  envsubst < /app/rtl_airband.conf > /tmp/rtl_airband.conf\n\
  /app/rtl_airband -F -e -c /tmp/rtl_airband.conf' > /entrypoint.sh
RUN chmod 755 /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]

# Switch to a non-root user
RUN userdel --remove ubuntu && \
  groupadd --gid "${GID}" app && \
  useradd --uid "${UID}" --gid "${GID}" --shell /bin/bash --create-home app
USER app:app
