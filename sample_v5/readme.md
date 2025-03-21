send the data to the server in a shuffled manner.


leecher
gcc leecher.c meta.c -o leecher -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto && ./meta

Seeder
gcc -I$(brew --prefix openssl)/include -L$(brew --prefix openssl)/lib -lssl -lcrypto -Wno-deprecated-declarations seeder.c database.c -o seeder  && ./seeder


meta
gcc meta.c -o meta -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto && ./meta


bitfield
gcc bitfield.c -o bitfield -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto && ./bitfield

tracker
gcc meta.c database.c tracker.c -o tracker  -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto -Wno-deprecated-declarations &&  ./tracker


FOR DOCKER CONTAINERS
leecher
gcc leecher.c meta.c -o leecher -lssl -lcrypto -Wno-deprecated-declarations && ./leecher

seeder
gcc seeder.c database.c -o seeder -lssl -lcrypto -Wno-deprecated-declarations && ./seeder

meta
gcc meta.c database.c tracker.c -o tracker -lssl -lcrypto -Wno-deprecated-declarations && ./tracker

bitfield
gcc bitfield.c -o bitfield -lssl -lcrypto -Wno-deprecated-declarations && ./bitfield

tracker
gcc meta.c database.c tracker.c -o tracker -lssl -lcrypto -Wno-deprecated-declarations && ./tracker
