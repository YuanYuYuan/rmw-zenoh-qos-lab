FROM ros:rolling

ARG DEBIAN_FRONTEND=noninteractive

# Use Taiwan mirrors
COPY ./taiwan-sources-noble.list /etc/apt/sources.list

# Update & upgrade
RUN apt update -y
RUN apt upgrade -y
RUN apt dist-upgrade -y

# Install the utilities
RUN apt install -y  \
    clang \
    clangd \
    curl \
    fzf \
    heaptrack \
    htop \
    iproute2 \
    iputils-ping \
    lldb \
    llvm \
    parallel \
    ripgrep \
    sccache \
    stow \
    tig \
    tmux \
    unzip \
    valgrind \
    wget \
    zsh

# ROS2 related
RUN apt install -y \
    ros-rolling-rmw-cyclonedds-cpp \
    ros-rolling-demo-nodes-cpp \
    ros-rolling-ament-cmake-vendor-package \
    ros-rolling-performance-test-fixture \
    ros-rolling-mimick-vendor \
    ros-rolling-google-benchmark-vendor \
    ros-rolling-osrf-testing-tools-cpp \
    ros-rolling-test-interface-files \
    ros-rolling-rcl-interfaces \
    ros-rolling-test-msgs \
    ros-rolling-turtlesim \
    ros-rolling-teleop-twist-keyboard \
    ros-rolling-rviz2 \
    ros-rolling-nav2-minimal-tb4-sim \
    python3-typing-extensions \
    ros-rolling-geographic-msgs \
    python3-pytest-timeout \
    python3-fastjsonschema \
    libpyside2-dev \
    libshiboken2-dev \
    pyqt5-dev \
    python3-pyqt5 \
    python3-pyqt5.qtsvg \
    python3-pyside2.qtsvg \
    python3-sip-dev \
    shiboken2 \
    python3-pytest-cov \
    clang-format \
    clang-tidy \
    python3-pytest-mock \
    python3-mypy \
    liblttng-ctl-dev \
    python3-cairo \
    python3-pil \
    python3-matplotlib \
    tango-icon-theme \
    python3-pydot \
    python3-pygraphviz \
    libasio-dev \
    libqt5svg5-dev \
    pyflakes3 \
    python3-babeltrace \
    cargo

RUN apt install -y nlohmann-json3-dev iproute2 iperf3 iputils-ping

# Based on ROS2 image
ARG USERNAME=ubuntu
RUN groupmod -g 985 ubuntu
RUN echo "${USERNAME}:${USERNAME}" | chpasswd

# Switch to user home directory
USER ${USERNAME}
WORKDIR /home/${USERNAME}
RUN mkdir -p ~/.local/bin
RUN mkdir -p ~/.cargo
RUN echo "[build]\njobs = 4" > ~/.cargo/config.toml
RUN echo "source /ws/install/setup.bash" > ~/.bashrc
RUN echo "export PATH=\"~/.local/bin:\$PATH\"" >> ~/.bashrc
RUN cd ~/.local/bin && wget -O- https://github.com/nushell/nushell/releases/download/0.106.1/nu-0.106.1-x86_64-unknown-linux-gnu.tar.gz | tar xzf - --strip-components=1


# # Install rmw_zenoh
# RUN mkdir src
# RUN git clone https://github.com/ZettaScaleLabs/rmw_zenoh -b bump_zenoh src/rmw_zenoh
# RUN bash -c "source /opt/ros/rolling/setup.bash && colcon build --cmake-args -DCMAKE_BUILD_TYPE=Debug"
