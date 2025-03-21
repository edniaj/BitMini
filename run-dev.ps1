# PowerShell script to run dev container
docker rm -f c-devbox 2>$null  # optional: remove existing container if exists

docker run -dit --name c-devbox `
  -v "${PWD}:/workspace" `
  -p 5555:5555 -p 6000:6000 -p 6001:6001 -p 6002:6002 `
  c-dev-env
