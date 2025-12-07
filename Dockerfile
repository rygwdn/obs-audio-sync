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
RUN eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)" && \
    brew tap obsproject/tools && \
    brew install --quiet obsproject/tools/clang-format@19 obsproject/tools/gersemi && \
    ln -sf /home/linuxbrew/.linuxbrew/opt/clang-format@19/bin/clang-format-19 /usr/local/bin/clang-format-19 && \
    update-alternatives --install /usr/bin/clang-format clang-format /home/linuxbrew/.linuxbrew/opt/clang-format@19/bin/clang-format-19 100

# Set up working directory
WORKDIR /workspace

# Set up environment for build scripts
ENV CCACHE_DIR=/workspace/.ccache
ENV QT_VERSION=6

# Default command
CMD ["/bin/zsh"]
