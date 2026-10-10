# UnbiasedStrata Windows

An experimental native Windows fork of [UnbiasedStrata by Robert Karl (robertkarl)](https://github.com/robertkarl/UnbiasedStrata), based on the [Strata engine by Niko1221 and contributors](https://github.com/Niko1221/Strata).

This is a community Windows port, not an official release of either upstream project. It preserves their MIT license and attribution. It does not claim to be a complete security audit or a categorically safer engine.

## Preview status

Windows x64, AVX2 CPU, one NVIDIA RTX 5090 (`sm_120`), and 128 GB installed RAM are the current target. Build and model-free process/HTTP tests are checked locally. **Model loading, generation correctness, speed, VRAM usage, streaming, tool calls and cancellation with a real model have not been tested on this Windows port.** GPU inference was deliberately excluded because the test machine's VRAM was occupied. Downloads contain no model weights or personal configuration.

Upstream UnbiasedStrata supports Linux and GSQ-RCO IQ3_XXS. This Windows fork additionally identifies IQ3_S in its API, using the existing upstream engine's quantization support; its inference is also untested. UD-IQ4_XS is not supported by this frontend's two-shard launcher. Use upstream Strata separately for that variant.

## Changes from UnbiasedStrata

- Native Windows child processes and binary pipes replace `fork`, `/proc/self/exe` and POSIX pipe handling.
- An owned Windows Job Object terminates the engine if its frontend disappears. Only required pipe handles are inherited.
- Windows uses `--ple-io mmap`; the engine rejects the Linux locked-RAM option on Windows.
- HTTP listens only on loopback. All endpoints require a Bearer API key read from a local file.
- Host and Origin checks reject DNS-rebinding hosts and foreign browser origins. There is no cross-origin access configuration.
- Request bodies are capped at 1 MiB. Vision, MCP and installer automation are not provided by this frontend.
- HTTP worker counts and the waiting queue are bounded; incomplete request reads time out after 10 seconds.
- API key files must be small regular files on a local disk. UNC, device and reparse-point paths are rejected.
- Extra engine arguments use an allowlist with bounded values; they cannot replace protected model paths or context settings.
- Runtime starts never fetch executables, dependencies or models.

The engine source remains UnbiasedStrata's unchanged snapshot of Strata **0.1.39**, not the latest official Strata. Engine/model parser code is not independently audited. Local model files and engine arguments must be trusted. Read [UPSTREAM.md](../UPSTREAM.md) for provenance.

## Run a downloaded preview

Extract the Windows package. Provide already prepared model files in a separate data directory:

```
data/models/IQ3_XXS/Qwen3.8-Flash-Next-GSQ-RCO-IQ3_XXS-00001-of-00002.gguf
data/models/IQ3_XXS/Qwen3.8-Flash-Next-GSQ-RCO-IQ3_XXS-00002-of-00002.gguf
data/packs/iq3_xxs/index.txt
data/packs/iq3_xxs/dense.bin
data/packs/iq3_xxs/native_experts.txt
data/packs/iq3_xxs/conversions.json
data/packs/iq3_xxs/expert-profile.bin
data/mtp/rt/experts.bin
data/mtp/rt/dense.bin
data/mtp/rt/dense.txt
data/mtp/rt/draft_vocab.bin
```

The exact model revision is `ed59f92082b1e93c0e96d60a8b11aab089b52f09` from [ISTA-DASLab](https://huggingface.co/ISTA-DASLab/Qwen3.8-Flash-Next-GSQ-RCO-GGUF/tree/ed59f92082b1e93c0e96d60a8b11aab089b52f09). Models have their own license. Prepared packs and draft files are required; they are not included. Upstream's one-time Python preparation commands are documented in UPSTREAM.md. Python is not required by this fork's running server.

```powershell
.\windows\Start-UnbiasedStrata.ps1 -DataDirectory 'D:\AI\StrataData' -CheckOnly
.\windows\Start-UnbiasedStrata.ps1 -DataDirectory 'D:\AI\StrataData' -Model IQ3_XXS
```

The launcher creates a random API key at `DataDirectory/private/api-key.txt`. Keep it private. API base URL: `http://127.0.0.1:8080/v1`. Clients must send `Authorization: Bearer <your key>`, including health checks. Never add the key to a public repository. A conservative free-RAM check precedes launch; passing it does not prove sufficient VRAM.

## Build and model-free tests

Use Visual Studio 2022 C++ Build Tools, CMake 3.24+ and CUDA 13.0+ for RTX 50. CUDA **13.2.78 is unsuitable** because of the documented Blackwell IQ3_S compiler issue; use the fixed **13.2.86** or another upstream-confirmed toolchain. [Compiler issue](https://github.com/ggml-org/llama.cpp/issues/28581).

Clone the repository and initialize the pinned llama.cpp submodule. In an x64 Visual Studio developer shell:

```powershell
git submodule update --init
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTRATA_ENABLE_CUDA=ON -DSTRATA_NATIVE_EXPERTS=ON -DSTRATA_PORTABLE=ON -DSTRATA_BUILD_TESTS=OFF -DCMAKE_CUDA_ARCHITECTURES=120 -DBUILD_SHARED_LIBS=OFF -DSTRATA_WINDOWS_TESTS=ON
cmake --build build-windows -j 4 --target unbiased-strata windows-process-test windows-http-test
ctest --test-dir build-windows -R windows- --output-on-failure
```

Source dependencies are pinned; the build itself makes no downloads. The preview bundles required NVIDIA runtime libraries with their notices, not a driver or compiler. Build output hashes accompany each download. The binary is not Authenticode-signed.

## Credits

- Robert Karl / robertkarl: UnbiasedStrata's C++ frontend, dependency pinning and reduced runtime surface.
- Niko1221 and the Strata contributors: inference engine and model preparation tools.
- llama.cpp, ggml and cpp-httplib contributors: tokenizer, chat templates, CPU backend and HTTP implementation.
- This fork: local Windows process port, launch helpers and HTTP access controls.

Automated coding assistance was used for this Windows port. Treat it as an experimental preview pending independent review and real inference validation.

## Experimental DeepSeek-V4.1-Flash backend (Windows)

A separate pinned llama.cpp backend can be selected with `--backend deepseek41`. It uses DeepSeek's architecture and tokenizer; the existing Strata engine continues to handle Qwen. See [DeepSeek Windows instructions](DEEPSEEK41-README.md) for build, model files, RAM guards and limitations. Native build and model-free tests are separate from real-model validation; GPU inference has not been tested on our computer. Models are not included or downloaded automatically.
