# Dockerfile for OBS Audio Sync Plugin build environment
# Supports both x86_64 and arm64 architectures

FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install basic build dependencies (including git which is required for Homebrew)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    jq \
    zsh \
    curl \
    ca-certificates \
    sudo \
    file \
    procps \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root user for Homebrew (Homebrew cannot be installed as root)
RUN useradd -m -s /bin/bash linuxbrew && \
    echo "linuxbrew ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Install Homebrew as the linuxbrew user (non-interactive)
USER linuxbrew
RUN /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)" < /dev/null

# Switch back to root and set up PATH
USER root
ENV PATH="/home/linuxbrew/.linuxbrew/bin:/home/linuxbrew/.linuxbrew/sbin:${PATH}"

# Install zsh via Homebrew (run as linuxbrew user to avoid permission issues)
RUN sudo -u linuxbrew /home/linuxbrew/.linuxbrew/bin/brew install --quiet zsh || \
    (echo "Homebrew zsh installation failed, using apt zsh" && \
     apt-get update && apt-get install -y --no-install-recommends zsh && \
     rm -rf /var/lib/apt/lists/*)

# Install OBS Studio dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgles2-mesa-dev \
    libsimde-dev \
    && rm -rf /var/lib/apt/lists/*

# Add OBS PPA and install OBS Studio development packages
RUN apt-get update && apt-get install -y --no-install-recommends \
    software-properties-common \
    && add-apt-repository --yes ppa:obsproject/obs-studio \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
    obs-studio \
    libobs-dev \
    libobs0 \
    && rm -rf /var/lib/apt/lists/*

# Install Qt6 dependencies (including Qt6 Test and private headers)
RUN apt-get update && apt-get install -y --no-install-recommends \
    qt6-base-dev \
    libqt6svg6-dev \
    qt6-base-private-dev \
    qt6-tools-dev \
    qt6-base-dev-tools \
    && rm -rf /var/lib/apt/lists/*

# Install FFmpeg development packages (for Linux builds)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    && rm -rf /var/lib/apt/lists/*

# Install ccache for faster builds
RUN apt-get update && apt-get install -y --no-install-recommends \
    ccache \
    && rm -rf /var/lib/apt/lists/*

# Install Xvfb for headless GUI testing
RUN apt-get update && apt-get install -y --no-install-recommends \
    xvfb \
    x11vnc \
    x11-utils \
    && rm -rf /var/lib/apt/lists/*

# Install clang-format and gersemi for linting
# Install from obsproject/tools tap (matching CI setup)
# Run as linuxbrew user to avoid permission issues
RUN sudo -u linuxbrew /home/linuxbrew/.linuxbrew/bin/brew tap obsproject/tools && \
    sudo -u linuxbrew /home/linuxbrew/.linuxbrew/bin/brew install --quiet obsproject/tools/clang-format@19 obsproject/tools/gersemi && \
    ln -sf /home/linuxbrew/.linuxbrew/opt/clang-format@19/bin/clang-format-19 /usr/local/bin/clang-format-19 && \
    update-alternatives --install /usr/bin/clang-format clang-format /home/linuxbrew/.linuxbrew/opt/clang-format@19/bin/clang-format-19 100 || \
    (echo "Warning: Homebrew installation failed, falling back to apt packages" && \
     apt-get update && apt-get install -y --no-install-recommends \
         wget gnupg ca-certificates && \
     wget -qO - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - && \
     echo "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-19 main" >> /etc/apt/sources.list.d/llvm.list && \
     apt-get update && apt-get install -y --no-install-recommends \
         clang-format-19 python3 python3-pip && \
     pip3 install --no-cache-dir --break-system-packages gersemi && \
     update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-19 100 && \
     rm -rf /var/lib/apt/lists/*)

# Set up working directory
WORKDIR /workspace

# Set up environment for build scripts
ENV CCACHE_DIR=/workspace/.ccache
ENV QT_VERSION=6

# No default CMD - let devcontainers handle the entrypoint/command
# This allows devcontainers to properly override for initialization commands
