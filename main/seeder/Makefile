# Makefile for tracker and peer

# Compiler and flags
CC       := gcc
CFLAGS   := -Wall -Wextra -Wno-deprecated-declarations
LDFLAGS  := -lssl -lcrypto

# Source files
TRACKER_SRCS := meta.c database.c tracker.c parser.c
PEER_SRCS    := peer.c database.c meta.c bitfield.c seed.c leech.c peerCommunication.c

# Object files (automatically derived)
TRACKER_OBJS := $(TRACKER_SRCS:.c=.o)
PEER_OBJS    := $(PEER_SRCS:.c=.o)

# Default target builds everything
.PHONY: all clean
all: tracker peer

# Build tracker
tracker: $(TRACKER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build peer
peer: $(PEER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Generic rule to compile .c → .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run tracker
.PHONY: run-tracker
run-tracker: tracker
	./tracker

# Run peer
.PHONY: run-peer
run-peer: peer
	./peer

# Clean up
clean:
	rm -f $(TRACKER_OBJS) $(PEER_OBJS) tracker peer
