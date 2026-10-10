# DeepSeek-V4.1-Flash on native Windows (experimental)

This Windows fork of **UnbiasedStrata by Robert Karl (robertkarl)** adds a separately built DeepSeek backend. The existing Qwen path still uses the original Strata engine by Niko1221 and contributors. The DeepSeek path uses the **JigSawPT llama.cpp `dsv41-porte` port**, with a small local Windows and HTTP patch. It does not translate DeepSeek into Qwen or claim that Strata's Qwen kernels support DeepSeek.

## Status

Build and model-free test results are recorded in `DEEPSEEK41-BUILD.json` when a download package is produced. **No DeepSeek model inference, output quality, VRAM allocation or speed has been validated on our computer.** GPU resources were occupied. This is an experimental integration, not a validated model runtime or an audited security guarantee.

## Source and model

- Official model: https://huggingface.co/deepseek-ai/DeepSeek-V4.1-Flash
- Compatible converted model: https://huggingface.co/JigSawPT/DeepSeek-V4.1-Flash-GGUF (approximately 502 GB in 11 shards).
- Backend: https://github.com/JigSawPT/llama.cpp/tree/dsv41-porte
- Pinned commit: `8ce0968539b1a6795f3413c2783dfa5646b4688a`. Archive SHA-256: `7eb842a9d3164d19a015794aadb13e91165ba607222e461e48a957033095dcae`.
- Author's technical report: https://github.com/JigSawPT/deepseek-v41-flash-on-5090

DeepSeek V4, V4.1, R1 and distilled R1 models have different architectures. This mode specifically requires `general.architecture=deepseek41`. Download **every shard of the same conversion revision** and select `...-00001-of-00011.gguf`. The launcher checks all shard headers and split indices, but does not verify model download hashes. Keep independently verified model files on a local NVMe SSD. Model weights are not included and are not downloaded automatically.

No Qwen pack, expert profile or Qwen MTP directory is used. The separately compiled backend handles DeepSeek tokenization, templates, attention, engram memory and SSD expert streaming. DSpark speculative decoding is deliberately not enabled in this first integration.

## Build

Use Windows x64, Visual Studio 2022 Build Tools (C++ workload), CMake, Ninja, Python 3.12 or newer and CUDA 13.2.86 (or a suitable newer CUDA 13 toolkit). An NVIDIA driver compatible with the toolkit is required for inference. This build targets RTX 5090 / `sm_120` and an AVX2 CPU. Other GPUs are outside this package's scope.

Initialize the original pinned llama.cpp submodule, then build the frontend as described in `windows/README.md`. From an x64 Visual Studio developer PowerShell:

```powershell
.\windows\Build-Windows-DeepSeek41.ps1 -CudaToolkitPath 'C:\CUDA\13.2.86'
```

The preparation script downloads a source archive at the fixed commit, requires its exact SHA-256, applies only matching patch hunks and refuses unknown edits to patched files. It copies our shared access policy into the backend source. UI, server subprocess support and HTTPS download support are disabled at build time. This script builds and runs CPU-only tests; it does not start a model. Compiler warnings and successful compilation cannot prove numerical correctness on our hardware.

## Start

Provide a private local file with a 32-512 character printable ASCII API key. The existing Qwen launcher's key file can be reused. Keep the key file readable only by your Windows account. Use absolute local file paths; network paths and reparse redirects are rejected.

```powershell
.\windows\Start-DeepSeek41.ps1 `
  -ModelFile 'F:\Models\DeepSeek41\MODEL-00001-of-00011.gguf' `
  -ApiKeyFile 'C:\Strata\UnbiasedStrata\data\private\api-key.txt' `
  -CheckOnly
```

Remove `-CheckOnly` when GPU resources and verified weights are available. `-CheckOnly` reads only bounded GGUF metadata and never invokes the backend. Exit code 0 means the preflight passed; 2 means an invalid/missing input; 3 means files passed but too little RAM is free. A preflight pass is not an inference test.

Defaults are a 4096-token context, 18 GiB VRAM expert cache, 64 GiB pinned RAM cache and four I/O workers. These are starting settings, not measured optimums. The RAM guard requires pinned cache plus 32 GiB free RAM for other allocations and OS headroom; it cannot predict exact peak usage or check available VRAM. Reduce `-ExpertRamGiB` or free RAM as needed. Context is currently limited to 1024-8192. SSD direct reads are enabled; the engine may fall back to buffered I/O depending on the filesystem. Reasoning mode is off by default.

The backend starts as an owned Windows Job process. Closing the frontend or pressing Ctrl+C terminates its process tree. No unrelated Strata server is stopped. The original installed official Strata remains separate.

## API and protections

Use `http://127.0.0.1:8080/v1`, Bearer authentication and model ID `DeepSeek-V4.1-Flash`.

- Only `GET /health`, `GET /v1/models` and `POST /v1/chat/completions` are exposed.
- Authentication applies to every exposed route, including health and model lists.
- Host and Origin checks match the local port; foreign/opaque origins and duplicate authorization headers are rejected.
- Requests are limited to 1 MiB, with a bounded queue and timeouts.
- Text chat and SSE streaming are supported by the backend. Media parts/URLs and backend control fields are rejected. Tool definitions describe calls; the API does not execute tools.
- The backend receives a filtered environment and explicit local arguments. No UI, MCP proxy, model router or arbitrary extra arguments are exposed.
- Key files must be regular local files; UNC, device, alternate stream and reparse paths are rejected.

These controls reduce HTTP exposure. They do not establish a complete audit of the model parser, streaming engine, CUDA kernels or third-party dependencies. The local executable, model files and local filesystem administration are trusted. Real-model correctness and performance remain untested here.

The source patch is in `backends/deepseek41/windows-hardening.patch`; original MIT licensing and third-party notices must accompany redistribution. Model licensing is separate.
