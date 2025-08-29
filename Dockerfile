FROM ros:rolling

ARG DEBIAN_FRONTEND=noninteractive

# Use Taiwan mirrors
COPY ./taiwan-sources-noble.list /etc/apt/sources.list

RUN apt update -y && \
    apt upgrade -y && \
    apt dist-upgrade -y

RUN apt install -y \
        cargo \
        iproute2 \
        ros-rolling-ament-cmake-vendor-package \
        ros-rolling-example-interfaces \
        ros-rolling-rmw-cyclonedds-cpp \
        nlohmann-json3-dev \
        wget \
        xz-utils \
        curl

# Install Nushell (latest release)
RUN mkdir -p /usr/local/bin && \
    cd /usr/local/bin && \
    wget -O- https://github.com/nushell/nushell/releases/download/0.106.1/nu-0.106.1-x86_64-unknown-linux-gnu.tar.gz \
    | tar xzf - --strip-components=1 && \
    chmod +x /usr/local/bin/nu

# Cargo settings
RUN mkdir -p /root/.cargo && \
    echo "[build]\njobs = 4" > /root/.cargo/config.toml

# ROS environment
RUN echo "source /ws/install/setup.bash" >> /root/.bashrc
