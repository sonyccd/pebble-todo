# pebble-todo

A todo list watchapp for Pebble watches. Add tasks by voice, mark them done, and delete them — all from your wrist. Tasks persist across app launches.

## Features

- **Voice input** — press select on "+ Add task" to dictate a new task via the Pebble microphone
- **Strikethrough** — completed tasks are crossed out in the list
- **Action menu** — select any task to toggle done/undone or delete it
- **Persistent storage** — up to 32 tasks survive app restarts

## Building & running

```sh
pebble build                        # compile for all target platforms
pebble install --emulator emery     # run on the Pebble Time 2 emulator
pebble install --phone <ip>         # sideload to a paired phone
pebble logs --emulator emery        # stream log output
```

## CI

Every pull request is built automatically via GitHub Actions. See `.github/workflows/build.yml`.

## SDK setup

Install `uv`, then:

```sh
uv tool install pebble-tool --python 3.13
pebble sdk install latest
```

Ubuntu dependencies: `libsdl2-2.0-0 libglib2.0-0 libpixman-1-0 zlib1g libsndio7.0`

Full SDK docs: <https://developer.repebble.com>
