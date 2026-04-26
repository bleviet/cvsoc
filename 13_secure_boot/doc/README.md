# Phase 9: Cybersecurity (Educational Secure Boot)

> **Phase:** 9 (Bonus/Cross-cutting)
> **Goal:** Understand the principles of securing embedded systems through image signing and encryption without permanently locking your hardware.

This project implements an **Educational Secure Boot** flow for the DE10-Nano. We focus on cryptography, the Chain of Trust, and bitstream encryption (AES-256) while utilizing **zero-key modes and volatile RAM** to ensure you do not permanently damage or lock down your development board.

---

## ⚠️ CRITICAL WARNING: eFuse Lockdown

The Cyclone V SoC supports storing encryption keys in non-volatile eFuses (OTP - One Time Programmable memory). **Blowing the eFuses is permanent.**

If you program an encryption key into the eFuses, the FPGA will **only** ever accept bitstreams encrypted with that exact key. If you lose the key file, your DE10-Nano's FPGA fabric becomes permanently unusable.

> **For this tutorial:** We will **never** program the eFuses. We will solely use the Battery-Backed RAM (BBRAM) for volatile key storage or analyze the encrypted output offline. If power is removed, the BBRAM key is cleared, and the board remains safely unlocked.

---

## The Secure Boot Concepts

### 1. Cryptographic Fundamentals
- **Symmetric Encryption (AES-256):** Used to encrypt the `.sof` bitstream into an `.rbf`. Both Quartus and the FPGA need the *exact same key* to encrypt and decrypt the data. This provides **Confidentiality**.
- **Asymmetric Encryption (RSA) & Hashing (SHA):** Used for Digital Signatures during the software boot sequence (U-Boot/Linux). The private key signs a hash of the image, and the public key (embedded in the bootloader) verifies it. This provides **Authenticity** and **Integrity**.

### 2. The Chain of Trust
Secure Boot guarantees that only authorized code runs on your system. It works by establishing a chain:
1.  **BootROM (Hardware):** Unchangeable code baked into the ARM silicon. It verifies the Preloader (U-Boot SPL).
2.  **U-Boot SPL:** Verifies the full U-Boot bootloader.
3.  **U-Boot:** Verifies the Linux Kernel, the Device Tree, and optionally the FPGA Bitstream.
4.  **Linux Kernel:** Mounts an encrypted or verified root filesystem (e.g., dm-verity).

If any step fails verification, the boot sequence halts.

---

## Hands-on: Encrypting the FPGA Bitstream

We have provided automation to generate development keys and encrypt the FPGA bitstream created in Phase 3 (`05_hps_led`).

### Step 1: Generate an AES-256 Key

Run the following command to generate a random 256-bit key in the format required by `quartus_cpf`:

```bash
make keys
```

This creates `aes_key.key`. The file contains a single line matching the required syntax: `KEY dev_key_1 = <64_hex_digits>;`

### Step 2: Encrypt the Bitstream

To convert the unencrypted `.sof` from Phase 3 into a secured `.rbf` using your new key, run:

```bash
make encrypt
```

This command runs `quartus_cpf` (inside the Docker container) to produce two files:
1.  `de10_nano_encrypted.rbf`: The compressed, encrypted bitstream. If you try to load this into an FPGA without the key, it will fail immediately.
2.  `de10_nano_encrypted.ekp`: The **Encryption Key Programming** file. This is what you program into the FPGA via JTAG (`quartus_pgm`) to store the key in volatile memory (BBRAM).

### Step 3 (Educational): Inspecting the Encrypted File

If you compare the unencrypted `.rbf` from Phase 6 (`10_linux_led/de10_nano.rbf`) to the newly generated `de10_nano_encrypted.rbf`, you will notice that the encrypted file looks like completely random high-entropy data, while the unencrypted file has visible repeating patterns (zeros, padding, block headers) typical of an FPGA configuration stream.

---

## Simulating the Chain of Trust (Software)

To implement Secure Boot on the ARM processor (HPS) side without permanent hardware changes, the standard approach involves **U-Boot FIT (Flattened Image Tree) images**.

A FIT image wraps the Linux Kernel, Device Tree, and FPGA Bitstream into a single file (`.itb`) and appends an RSA digital signature. This allows U-Boot to verify the integrity and authenticity of everything it loads.

### Hands-on: Signing a FIT Image

We have provided tools to generate an RSA key pair and sign a firmware image. This process packs the kernel, device tree, and bitstream generated in Phase 6 (`10_linux_led`) into a single signed blob.

#### Step 1: Generate an RSA Key Pair

Run the following command to generate a 2048-bit RSA private and public key pair:

```bash
make rsa_keys
```

This generates `keys/dev_key.key` (the private key used to sign) and `keys/dev_key.pub` (the public key that would normally be compiled into U-Boot).

#### Step 2: Build and Sign the FIT Image

Run the following command to pack and sign the firmware:

```bash
make sign_fit
```

This runs `mkimage` inside a Docker container (downloading necessary tools automatically). It takes the instructions from `fit_image.its` to bundle the Linux kernel (`zImage`), Device Tree (`socfpga_cyclone5_de0_nano_soc.dtb`), and FPGA bitstream (`de10_nano.rbf`), and then signs the entire bundle using your RSA private key. 

The output is `signed_firmware.itb`.

### How it works in a real product:
1.  You generate an RSA private/public key pair (which we just did).
2.  You use the `mkimage` tool (part of U-Boot) to build the FIT image and sign it with your private key (which we just did).
3.  **Next Step (Not covered in this tutorial):** You embed the RSA public key directly into the U-Boot device tree (`u-boot.dtb`).
4.  You configure U-Boot to `CONFIG_FIT_SIGNATURE=y` and `CONFIG_RSA=y`.
5.  When U-Boot attempts to `bootm` the FIT image, it halts if the signature does not perfectly match the public key.

> *Note: Implementing full U-Boot FIT verification requires rebuilding the Phase 6 Buildroot image with custom U-Boot configuration flags. This folder focuses on providing the foundational scripts and understanding of the signing process.*

---

## Clean Up

To remove the generated keys and encrypted bitstreams:

```bash
make clean
```
