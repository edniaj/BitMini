send the data to the server in a shuffled manner.


leecher
gcc leecher.c meta.c -o leecher -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto && ./meta

Seeder
gcc -I$(brew --prefix openssl)/include -L$(brew --prefix openssl)/lib -lssl -lcrypto -Wno-deprecated-declarations seeder.c database.c -o seeder  && ./seeder


meta
gcc meta.c -o meta -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto && ./meta

bitfield
gcc bitfield.c -o bitfield -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto && ./bitfield