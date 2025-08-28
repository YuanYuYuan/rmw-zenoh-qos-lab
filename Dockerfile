FROM ros:rolling

ARG DEBIAN_FRONTEND=noninteractive

# Use Taiwan mirrors
COPY ./taiwan-sources-noble.list /etc/apt/sources.list

RUN apt update -y
RUN apt upgrade -y
RUN apt dist-upgrade -y

RUN apt install -y \
    cargo \
    iproute2 \
    ros-rolling-ament-cmake-vendor-package \
    ros-rolling-example-interfaces \
    nlohmann-json3-dev \
    wget

RUN mkdir -p ~/.local/bin
RUN mkdir -p ~/.cargo
RUN echo "[build]\njobs = 4" > ~/.cargo/config.toml
RUN echo "source /ws/install/setup.bash" > ~/.bashrc
RUN echo "export PATH=\"~/.local/bin:\$PATH\"" >> ~/.bashrc
RUN cd ~/.local/bin && wget -O- https://github.com/nushell/nushell/releases/download/0.106.1/nu-0.106.1-x86_64-unknown-linux-gnu.tar.gz | tar xzf - --strip-components=1
