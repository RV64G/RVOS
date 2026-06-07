# Use a standard Ubuntu base image for broad compatibility.
FROM ubuntu:22.04

# Avoid interactive prompts during package installation.
ENV DEBIAN_FRONTEND=noninteractive

COPY tools/deps/ubuntu-apt.txt /tmp/rvos-ubuntu-apt.txt

# Install host dependencies from the shared Ubuntu package manifest.
RUN apt-get update && apt-get install -y --no-install-recommends \
    $(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' /tmp/rvos-ubuntu-apt.txt) \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root user 'vscode' to enhance security and avoid permission issues.
# This user is granted passwordless sudo access.
RUN useradd -m -s /bin/bash vscode \
    && echo "vscode ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/vscode

# Switch the context to the non-root user.
USER vscode

# Set the default working directory for the container.
WORKDIR /workspaces/RVOS
