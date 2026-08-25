# scripts/docker/

Helper scripts for the `deploy/docker/docker-compose.yml` stack.

## `compose-override.sh` — absolute-path override generator

**Why:** docker compose v5.3.1 has a buildx-bake path-resolution bug (#45):
`docker compose up`/`build` resolve relative `build.context`,
`build.dockerfile`, and bind-mount `source` paths against an unrelated base
instead of the compose file's directory. `docker compose config` renders the
paths correctly, but the build step (bake) uses a different, wrong base, so
the whole stack fails to build/start with errors like:

```
unable to prepare context: path "/home/<user>/frontends/admin" not found
```

**What it does:** generates a temp compose **override** file in which every
relative path is replaced by its resolved absolute form (derived from the real
compose file via `docker compose config --format json`, so it tracks
service/mount changes automatically). Absolute paths are immune to the bake
base-path bug.

**Usage:**

```bash
OVERRIDE="$(bash scripts/docker/compose-override.sh deploy/docker/docker-compose.yml)"
docker compose -f deploy/docker/docker-compose.yml -f "$OVERRIDE" --project-directory . up -d
rm -f "$OVERRIDE"
```

**Where it's wired in:** `manage.sh docker-up`/`docker-down` and
`benchmarks/fulla/setup.sh` both generate and layer the override
automatically, so `./manage.sh docker-up` and the benchmark one-click setup
work on affected compose versions without manual intervention.

**When to remove:** once the upstream compose/buildx bug is fixed and a
known-good version range is established, this override becomes unnecessary
(harmless but redundant). At that point the helper and its call sites can be
removed.
