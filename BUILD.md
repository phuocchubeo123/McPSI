# Build Guide (Verified)

These steps were verified on Ubuntu 24.04 with Bazel `6.5.0` (from `.bazelversion`).

## 1. Install Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y gcc g++ cmake ninja-build nasm make npm curl
sudo npm install -g @bazel/bazelisk
sudo ln -sf "$(command -v bazelisk)" /usr/local/bin/bazel
bazel --version
```

## 2. Prepare Third-Party Cache (Required)

This repo uses `--distdir=./thirdparty` in `make`. A transitive dependency currently tries to fetch libsodium from a dead upstream URL, so pre-populate it locally:

```bash
cd /McPSI
mkdir -p thirdparty
curl -fL -o thirdparty/libsodium-1.0.18.tar.gz \
  https://github.com/jedisct1/libsodium/releases/download/1.0.18-RELEASE/libsodium-1.0.18.tar.gz
```

## 3. Build

```bash
bazel build -c opt --distdir=./thirdparty --copt=-Wno-mismatched-new-delete --jobs=4 //...
```

## 4. Test (Working Command)

Run all tests without building unrelated non-test targets:

```bash
bazel test -c opt --distdir=./thirdparty --copt=-Wno-mismatched-new-delete --jobs=4 \
  --build_tests_only //...
```

## 5. `make test`

`make test` calls:

```bash
bazel test -c opt --distdir=./thirdparty --copt=-Wno-mismatched-new-delete --jobs=4 //...
```

After removing `bench_multi`, this target set no longer includes that missing file.

## 6. Run Examples

Single-process mode:

```bash
make run
```

Two-party mode (two terminals):

```bash
make run_p0
```

```bash
make run_p1
```

Or direct Bazel:

```bash
bazel run -c opt --distdir=./thirdparty //mcpsi/example:toy_psi
bazel run -c opt --distdir=./thirdparty //mcpsi/example:toy_mc_psi
bazel run -c opt --distdir=./thirdparty //mcpsi/example:mc_psi
```

Defaults come from `.env`: `SET0`, `SET1`, `CR`, `CACHE`, `THREAD`, `FAIRNESS`.

## 7. Run `triple_comm_bench` (TCP, Two Terminals)

Build it:

```bash
bazel build -c opt --distdir=./thirdparty //mcpsi/cr:triple_comm_bench
```

Run rank 0 (sender) in terminal 1:

```bash
bazel-bin/mcpsi/cr/triple_comm_bench \
  --rank=0 \
  --sender_addr=127.0.0.1 --sender_port=39530 \
  --receiver_addr=127.0.0.1 --receiver_port=39531 \
  --chunk=1024 --nums=10000 \
  --triples_out=/tmp/triples_rank0.csv \
  --key_out=/tmp/bdoz_key_rank0.txt
```

Run rank 1 (receiver) in terminal 2:

```bash
bazel-bin/mcpsi/cr/triple_comm_bench \
  --rank=1 \
  --sender_addr=127.0.0.1 --sender_port=39530 \
  --receiver_addr=127.0.0.1 --receiver_port=39531 \
  --chunk=1024 --nums=10000 \
  --triples_out=/tmp/triples_rank1.csv \
  --key_out=/tmp/bdoz_key_rank1.txt
```
