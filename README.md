# Clone
```bash
git submodule update --init --recursive
```

# Optionally using docker image
```bash
docker-compose run dev bash
cd /root/code/
```

# Compiling (Requires Linux)
```bash
cc *.c http_parser/http_parser.c
```
