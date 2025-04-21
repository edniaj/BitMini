# BitMini

BitMini is a lightweight peer-to-peer file sharing system inspired by BitTorrent protocols. It features a tracker for coordinating peers and a peer application for seeding and leeching files.

# DEMO EXAMPLE
https://www.youtube.com/watch?v=---rbbDYTvY

## Requirements

- C compiler (GCC recommended)
- OpenSSL library
- Docker (optional, for containerized development)

## Setup Instructions

### Option 1: Setup on Local System

1. Install OpenSSL:
   - **macOS**: `brew install openssl`
   - **Ubuntu/Debian**: `sudo apt install libssl-dev`
   - **Fedora/CentOS**: `sudo dnf install openssl-devel`

2. Clone the repository:
   ```
   git clone https://github.com/yourusername/BitMini.git
   cd BitMini
   ```

### Option 2: Setup with Docker

1. Build the Docker image:
   ```
   docker build -t c-dev-env .
   ```

2. Run the container with ports exposed:

   **Windows (PowerShell):**
   ```powershell
   # Use the provided PowerShell script
   ./run-dev.ps1
   ```

   **macOS/Linux (Bash):**
   ```bash
   # Remove existing container if it exists
   docker rm -f c-devbox 2>/dev/null
   
   # Run the container
   docker run -dit --name c-devbox \
     -v "$(pwd):/workspace" \
     -p 5555:5555 -p 6000:6000 -p 6001:6001 -p 6002:6002 \
     c-dev-env
   ```

3. Access the container shell:
   ```
   docker exec -it c-devbox bash
   ```

## Building and Running

### Using the Makefile (Recommended)

We provide a Makefile to simplify building and running:

```
# Build both tracker and peer
make

# Build and run the tracker
make run-tracker

# Build and run the peer
make run-peer

# Clean compiled binaries
make clean

# Rebuild from scratch
make rebuild
```

### Manual Compilation

If you prefer to compile manually:

#### Docker Environment:
```
# Compile and run the tracker
gcc meta.c database.c tracker.c parser.c -o tracker -lssl -lcrypto -Wno-deprecated-declarations && ./tracker

# Compile and run the peer
gcc peer.c database.c meta.c bitfield.c seed.c leech.c peerCommunication.c -o peer -lssl -lcrypto -Wno-deprecated-declarations && ./peer
```

#### Local System (macOS example):
##### You need to include the openssl library when compiling, we are using openssl for hashing our files !!
```
# Tracker
gcc meta.c database.c tracker.c parser.c -o tracker -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto -Wno-deprecated-declarations && ./tracker

# Peer
gcc peer.c database.c meta.c bitfield.c seed.c leech.c peerCommunication.c -o peer -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto -Wno-deprecated-declarations && ./peer
```

## System Architecture

BitMini consists of two main components:

1. **Tracker**: Coordinates peers and maintains information about available files
   - Tracks seeders for each file
   - Enforces policy-based restrictions (IP blocking, region restrictions)
   - Provides file metadata to peers

2. **Peer**: Handles both seeding and leeching operations
   - Can share files (seeding)
   - Can download files (leeching)
   - Communicates with both the tracker and other peers

## Usage

1. **Start the tracker first**:
   ```
   make run-tracker
   ```
   
2. **Start peer instances**:
   ```
   make run-peer
   ```
   
3. Follow the on-screen prompts in each application to share or download files.

## Network Ports

BitMini uses the following default ports:
- **5555**: Tracker server port
- **6000-6002**: Peer communication ports

## Archive
/development_archive/ - this folder is an archive of past iterative development before we reached our final version.