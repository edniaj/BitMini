# BitMini
Mini version of p2p - mini bittorent

Look inside the sample folder
example of how we open up ports to write to other clients. PEER 2 PEER 


brew install openssl

gcc meta.c -o meta -I/opt/homebrew/opt/openssl/include -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto

run docker
1. docker build -t c-dev-env .      | build docker file 
2. ./run-dev.ps1                    | to open up thhe ports 
3. docker exec -it c-devbox bash    | open shell
