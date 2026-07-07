# Crossfire Mapper

`crossfire-mapper` generates the HTML map browser (map pictures, region and
map indexes, item/monster/quest listings) from the maps tree.
`mapper-with-search.sh` wraps it: it runs the mapper with
`-build-search-data`, then builds the lunr client-side search index
(`search_index.js`) inside a `node:alpine` Docker container.

Without Docker installed, the wrapper fails after the mapper finishes:

```
./mapper-with-search.sh: 76: docker: not found
cp: cannot stat '/tmp/tmp.XXXXXXXX/search_index.js': No such file or directory
```

The generated site is intact, but `search_index.js` is missing, so search is
broken on every page until the index is built.

## Installing Docker

### Ubuntu / Debian

```sh
sudo apt update
sudo apt install docker.io
sudo systemctl enable --now docker
```

Ubuntu's `docker.io` package is sufficient; the upstream `docker-ce`
repository is not needed.

### Fedora

Fedora packages Docker Engine as `moby-engine`:

```sh
sudo dnf install moby-engine
sudo systemctl enable --now docker
```

Note: Fedora's default container tooling is Podman. If you prefer not to run
the Docker daemon, `sudo dnf install podman-docker` provides a
docker-compatible CLI that runs the same `node:alpine` container rootless —
no daemon, no `docker` group needed. Either works for this script.

### openSUSE (Leap / Tumbleweed)

```sh
sudo zypper install docker
sudo systemctl enable --now docker
```

## Allow your user to run Docker without sudo

The script calls plain `docker run`, which needs access to
`/var/run/docker.sock` (root or `docker` group only). On all distros above:

```sh
sudo usermod -aG docker $USER
```

**Then log out and back in** — group changes only apply to new login
sessions. Close the SSH connection and reconnect; tmux/screen panes created
earlier keep the old group list. To activate in the current shell only:
`newgrp docker`. Skipping this causes:

```
permission denied while trying to connect to the docker API at unix:///var/run/docker.sock
```

Confirm with `groups` — the list must include `docker`.
(Not needed with Fedora's rootless `podman-docker`.)

## Verify and pre-pull the image

```sh
docker run --rm node:alpine node --version
```

The first run pulls the image (needs internet access) and prints the
container's Node version. Success here means the daemon and permissions are
good.

If it fails:

- `sudo docker version` works but plain fails → socket permissions; redo the
  group step above.
- `systemctl status docker` → daemon not running; `sudo systemctl enable --now docker`.
- `ls -l /var/run/docker.sock` → expect `srw-rw---- root docker`; if not,
  `sudo systemctl restart docker`.

## Run the mapper

```sh
cd utils/mapper
./mapper-with-search.sh <your usual args>
```

Expect some `npm install` output, then an `indexing [====] ...` progress
bar. Verify the result:

```sh
ls -lh html/search_data.js html/search_index.js
```

Both files should exist; `search_index.js` is typically several MB for a
full maps run.

## Caveats

- **cron / CI:** the script uses `docker run -it ...`. The `-t` flag needs a
  TTY and fails non-interactively with `the input device is not a TTY`.
  Drop `-it` for unattended runs — it isn't needed interactively either.
- **Concurrent runs:** the container is started with
  `--name my-running-script`, so two simultaneous runs collide
  (`name already in use`). Remove `--name` if runs may overlap.
- **Network per run:** the container does `npm install lunr progress` on
  every invocation, so each run needs internet access and takes a few extra
  seconds.
- **Security:** members of the `docker` group are effectively root on the
  box; use a dedicated account if that matters. (Fedora's rootless
  `podman-docker` avoids this.)
