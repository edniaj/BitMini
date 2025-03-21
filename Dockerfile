FROM ubuntu:latest

# Install GCC, OpenSSL, and nano
RUN apt-get update && \
    apt-get install -y build-essential openssl libssl-dev nano

WORKDIR /workspace

CMD ["/bin/bash"]
