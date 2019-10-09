# Embedded Microcontroller Software-Update Toolkit

A highly optimized, ultra-low RAM footprint suite of C utilities designed for firmware self-update scenarios on resource-constrained microcontrollers (e.g., ARM Cortex-M series like STM32L1).

Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

## The Story
This project was born out of a specific challenge: implementing a secure, compressed firmware update mechanism on an STM32L1-series microcontroller with extremely limited RAM. 

The requirement was to handle gzipped firmware images stored in Flash memory. By leveraging the fact that the compressed data is already available in Flash, I was able to redesign a standard DEFLATE decompressor to minimize its working set. This optimization reduced the RAM footprint from a typical **~33KB down to just about 746 bytes**.

The result is a compact, dependency-free toolkit where decryption (AES), integrity verification (SHA256/CRC32), and decompression (DEFLATE) can be chained together in a streaming fashion. This allows for "on-the-fly" processing: decrypting and decompressing firmware payloads directly as they are being streamed from Flash to Program Memory, without ever needing to hold the full uncompressed payload in RAM.

## Key Features
*   **Ultra-Low RAM Footprint:** DEFLATE decompression requires only ~746 bytes of working memory.
*   **Zero Dependencies:** Standard C library only; ideal for bare-metal or RTOS environments.
*   **Streaming Architecture:** Uses a callback-based approach (`DeflateState`) to allow seamless chaining of cryptographic and decompression layers.
*   **End-to-End Security Toolkit:**
    *   **AES-128:** Optimized implementation (including 32-bit vectorized key expansion) for block decryption.
    *   **SHA-256:** NIST FIPS 180-4 compliant for payload integrity.
    *   **CRC-32:** Standard polynomial implementation for error detection.
*   **RAM-Optimized:** Designed with `EXECUTE_FROM_RAM` macros in mind, making it suitable for self-updating bootloaders that must run entirely from RAM to prevent overwriting themselves during the update process.

## Architecture & Integration

The core strength of this library is its **chainable pipeline**. Using the provided "glue" structures (like `CryptoFlashState`), you can implement a processing pipeline like this:

`Encrypted Flash Payload` $\rightarrow$ `AES Decryption Callback` $\rightarrow$ `DEFLATE Decompressor` $\rightarrow$ `Flash Writing Callback` $\rightarrow$ `Program Memory`

### Component Breakdown
| Module | Purpose | Implementation Note |
| :--- | :--- | :--- |
| **`inflate.c`** | DEFLATE (RFC 1951) Decoder | Uses a window-based bitstream reader and callback-driven output. |
| **`aes128.c`** | AES-128 Decryption | Includes optimized S-Box lookups to save Flash memory. |
| **`sha256.c`** | SHA-256 Hashing | Provides high-integrity verification for the firmware blob. |
| **`crc32.c`** | CRC-32 Checksum | Fast error detection for the transmission/decryption process. |
| **`selfupdate.c`**| The "Glue" Layer | Implements the `CryptoFlashState` to link AES and DEFLATE. |

## Getting Started

### Prerequisites
*   A C compiler (GCC recommended).
*   `make` build system.

### Building & Testing
The repository includes a comprehensive test suite that simulates the end-to-end update process, including decryption and decompression.

```bash
# Build all targets (tests and selfupdate library)
make all

# Run the inflation/decompression unit tests
make tests

# Run the end-to-end security integration tests
# (Tests AES decryption + DEFLATE decompression in a single pipeline)
./out/selfupdate-test 
```

### Integration into your MCU Project
1.  **Copy the source files:** Add the `.c` and `.h` files for the specific modules you need to your project.
2.  **Define your I/O callbacks:** Implement functions for `readByte`, `writeByte`, and `readBack` that interface with your hardware (e.g., reading from SPI Flash or writing to Internal Flash).
3.  **Configure memory sections:** For bootloader use cases, ensure the implementation is compiled to run from RAM (using `__attribute__((section(".data")))` or similar linker scripts).

## License
Apache 2.0 License
