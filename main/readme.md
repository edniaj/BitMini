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
gcc seeder.c database.c meta.c bitfield.c seed.c leech.c peerCommunication.c -o seeder -lssl -lcrypto -Wno-deprecated-declarations && ./seeder

meta
gcc meta.c database.c  tracker.c -o tracker -lssl -lcrypto -Wno-deprecated-declarations && ./tracker

bitfield
gcc bitfield.c -o bitfield -lssl -lcrypto -Wno-deprecated-declarations && ./bitfield

tracker
gcc meta.c database.c tracker.c parser.c -o tracker -lssl -lcrypto -Wno-deprecated-declarations && ./tracker

seeder
gcc seeder.c database.c meta.c bitfield.c seed.c leech.c peerCommunication.c -o seeder -lssl -lcrypto -Wno-deprecated-declarations && ./seeder

peer



parser
gcc parser.c parser.h -o parser && ./parser
1. build docker file 
docker build -t c-dev-env .  
2. to open up thhe ports 
./run-dev.ps1   
3. open shell
docker exec -it c-devbox bash


4d4e64929eb11c3170ef3125593ac8df5ebc14b2bb693b253e71220dd5258ee8
we will use this filehash (gray_cat.png) and block this 

Heavy documentation in peer.c , tracker.c, leech.c and seed.c