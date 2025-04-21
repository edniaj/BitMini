send the data to the server in a shuffled manner.


CLIENT
gcc client.c meta.c -o client -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto && ./meta

SERVER
gcc server.c -o server -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto && ./server
