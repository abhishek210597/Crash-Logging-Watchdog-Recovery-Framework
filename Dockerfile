# Production-grade Dockerfile for building PlatformIO ESP-IDF project
FROM python:3.9-slim

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV PLATFORMIO_CORE_DIR=/workspace/.platformio

# Install required system packages
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    scons \
    curl \
    unzip \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Install PlatformIO Core
RUN pip install --no-cache-dir -U platformio

# Create workspace directory
WORKDIR /workspace

# Pre-install toolchains and platforms for cezerio_dev_esp32c6
RUN pio platform install espressif32 --with-package=framework-espidf

# Define entry point to build the project
CMD ["pio", "run"]
