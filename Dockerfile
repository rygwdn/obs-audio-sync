# Dockerfile for OBS Audio Sync Plugin build environment
# Supports both x86_64 and arm64 architectures

FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install basic build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    jq \
    zsh \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Homebrew for zsh and other tools (non-interactive)
RUN /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)" < /dev/null || true
ENV PATH="/home/linuxbrew/.linuxbrew/bin:/home/linuxbrew/.linuxbrew/sbin:${PATH}"

# Install zsh via Homebrew if available, otherwise use apt
RUN (brew install --quiet zsh 2>/dev/null || apt-get install -y --no-install-recommends zsh) && \
    rm -rf /var/lib/apt/lists/*

# Install OBS Studio dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgles2-mesa-dev \
    libsimde-dev \
    && rm -rf /var/lib/apt/lists/*

# Add OBS PPA and install OBS Studio
RUN apt-get update && apt-get install -y --no-install-recommends \
    software-properties-common \
    && add-apt-repository --yes ppa:obsproject/obs-studio \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
    obs-studio \
    && rm -rf /var/lib/apt/lists/*

# Install Qt6 dependencies (including Qt6 Test)
RUN apt-get update && apt-get install -y --no-install-recommends \
    qt6-base-dev \
    libqt6svg6-dev \
    qt6-base-private-dev \
    qt6-tools-dev \
    && rm -rf /var/lib/apt/lists/*

# Install ccache for faster builds
RUN apt-get update && apt-get install -y --no-install-recommends \
    ccache \
    && rm -rf /var/lib/apt/lists/*

# Install clang-format and gersemi for linting
# Install LLVM 19 for clang-format 19.1.1+ (required by project)
RUN apt-get update && apt-get install -y --no-install-recommends \
    wget \
    gnupg \
    ca-certificates \
    && wget -qO - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - \
    && echo "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-19 main" >> /etc/apt/sources.list.d/llvm.list \
    && apt-get update && apt-get install -y --no-install-recommends \
    clang-format-19 \
    python3 \
    python3-pip \
    && pip3 install --no-cache-dir --break-system-packages gersemi \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-19 100

# Set up working directory
WORKDIR /workspace

# Set up environment for build scripts
ENV CCACHE_DIR=/workspace/.ccache
ENV QT_VERSION=6

# Default command
CMD ["/bin/zsh"]
