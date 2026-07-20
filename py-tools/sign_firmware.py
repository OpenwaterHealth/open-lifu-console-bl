#!/usr/bin/env python3
"""
sign_firmware.py  —  Create an SBSFU-compatible signed firmware image.

Crypto scheme: SECBOOT_ECCDSA_WITH_AES128_CBC_SHA256, used here in the
direct-flash (SECBOOT_USE_NO_LOADER) configuration:
  • A 320-byte header is prepended containing metadata, the SHA-256 of the
    firmware (FwTag), an AES-CBC IV field, and an ECDSA-P256/SHA-256
    signature over the first 128 authenticated bytes of the header.
  • The firmware body is stored IN CLEAR. At boot the bootloader verifies
    the header signature and compares SHA-256(slot) against FwTag; it does
    not decrypt. AES-CBC belongs to the (unused) OTA install path, so the
    IV field is populated only to satisfy the signed header format.

Output image layout (written to the active slot at 0x08010000 via DFU):

    Offset 0x000  [  4 B]  SFUMagic      "SFU1"
    Offset 0x004  [  2 B]  ProtocolVersion  0x0001
    Offset 0x006  [  2 B]  FwVersion     user-specified
    Offset 0x008  [  4 B]  FwSize        size of firmware body (bytes)
    Offset 0x00C  [  4 B]  PartialFwOffset  0
    Offset 0x010  [  4 B]  PartialFwSize    0
    Offset 0x014  [ 32 B]  FwTag         SHA-256(padded firmware)
    Offset 0x034  [ 32 B]  PartialFwTag  = FwTag (full image)
    Offset 0x054  [ 16 B]  InitVector    random (header field only, unused)
    Offset 0x064  [ 28 B]  Reserved      0x00...
    ─── end of authenticated region (128 bytes = 0x80) ──────────────────────
    Offset 0x080  [ 64 B]  HeaderSignature  ECDSA-P256(SHA-256(bytes 0..127))
    Offset 0x0C0  [ 96 B]  FwImageState  3 × 32 bytes of 0xFF (VALID marker)
    Offset 0x100  [ 32 B]  PrevHeaderFingerprint  0x00... (first install)
    ─── end of header (320 bytes = 0x140) ───────────────────────────────────
    Offset 0x140  [pad  ]  0xFF padding up to SFU_IMG_IMAGE_OFFSET
    Offset 0x400  [FwSize] Firmware body (clear)

Keys: by default both keys are read from the keys/ directory alongside this
script. Point the LIFU_BL_KEYS environment variable at a different keys
directory, or pass --private-key / --aes-key explicitly.

Upload the result via USB DFU to the active slot:

    dfu-util -D <output>.bin -a 0 -s 0x08010000:leave

Usage
-----
    python sign_firmware.py \\
        --firmware   path/to/app.bin \\
        --version    1               \\
        --output     signed_app.bin
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
except ImportError:
    sys.exit(
        "ERROR: 'cryptography' package not found.\n"
        "Activate the venv and run:  pip install -r requirements.txt"
    )

# ---------------------------------------------------------------------------
# SBSFU header constants
# ---------------------------------------------------------------------------
SFUMAGIC          = b"SFU1"
PROTOCOL_VERSION  = 1
HEADER_AUTH_LEN   = 128    # bytes signed by ECDSA
HEADER_SIGN_LEN   = 64     # ECDSA P-256 raw R||S
HEADER_STATE_LEN  = 96     # 3 × 32 bytes, initially 0xFF
HEADER_FP_LEN     = 32     # PrevHeaderFingerprint, 0x00 for first install
HEADER_TOTAL_LEN  = HEADER_AUTH_LEN + HEADER_SIGN_LEN + HEADER_STATE_LEN + HEADER_FP_LEN
# = 320 bytes (0x140)

# Offset, from the slot start, at which SBSFU expects the firmware body.
# MUST match SFU_IMG_IMAGE_OFFSET in SBSFU/App/Inc/sfu_fwimg_regions.h: the
# application is linked to run at slot base + this offset, and the slot image
# is [header 0x000..0x13F] [0xFF pad 0x140..0x3FF] [firmware @ 0x400].
# In the NO_LOADER configuration the image is flashed directly into the
# active slot, so the body must already sit at this offset.
IMAGE_OFFSET      = 0x400   # = SFU_IMG_IMAGE_OFFSET (1024 bytes)

# Padding granule for the firmware body. 32 is a conservative common
# multiple of the AES block size and of every target's minimum flash
# programming unit, and keeps FwSize flash-word aligned on all of them.
FLASH_WORD        = 32


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def default_keys_dir() -> Path:
    """Directory holding the signing keys. Defaults to the keys/ directory
    alongside this script; override with the LIFU_BL_KEYS environment
    variable (or per-file --private-key / --aes-key arguments)."""
    env = os.environ.get("LIFU_BL_KEYS")
    if env:
        return Path(env)
    return Path(__file__).resolve().parent / "keys"


def _pad_to_multiple(data: bytes, multiple: int, pad_byte: int = 0xFF) -> bytes:
    """Pad data to the next multiple of `multiple` bytes."""
    remainder = len(data) % multiple
    if remainder == 0:
        return data
    return data + bytes([pad_byte] * (multiple - remainder))


def _ecdsa_sign_raw(private_key, data: bytes) -> bytes:
    """
    Sign data with ECDSA-P256/SHA-256.
    Returns the 64-byte raw signature: R (32 bytes big-endian) || S (32 bytes big-endian).
    """
    der_sig = private_key.sign(data, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_sig)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


# ---------------------------------------------------------------------------
# Main signing function
# ---------------------------------------------------------------------------

def sign_firmware(
    firmware_bin: bytes,
    private_key_pem: bytes,
    aes_key: bytes,
    fw_version: int = 1,
) -> bytes:
    """
    Sign a raw firmware binary for SBSFU (clear body, NO_LOADER).

    Parameters
    ----------
    firmware_bin    : raw application .bin (starts at application load address)
    private_key_pem : PEM-encoded ECDSA P-256 private key
    aes_key         : 16-byte AES-128 key (validated; header format only)
    fw_version      : 16-bit firmware version number (≥ 1)

    Returns
    -------
    bytes — complete signed image: header + padding + clear firmware body
    """

    if len(aes_key) != 16:
        raise ValueError(f"AES key must be 16 bytes, got {len(aes_key)}")

    # Load private key
    priv = serialization.load_pem_private_key(private_key_pem, password=None)

    # Step 1: Pad the firmware to a multiple of FLASH_WORD bytes.
    plaintext = _pad_to_multiple(firmware_bin, FLASH_WORD, 0xFF)

    # Step 2: SHA-256 of the padded firmware -> FwTag (checked at every boot).
    fw_tag = hashlib.sha256(plaintext).digest()   # 32 bytes

    # Step 3: Random IV for the header's InitVector field. The field is part
    # of the ECDSA-signed header format but unused at boot (clear body).
    iv      = os.urandom(16)
    fw_size = len(plaintext)

    # Step 4: Build the 128-byte authenticated header part.
    # struct layout (little-endian):
    #   4s  SFUMagic
    #   H   ProtocolVersion
    #   H   FwVersion
    #   I   FwSize
    #   I   PartialFwOffset
    #   I   PartialFwSize
    #   32s FwTag
    #   32s PartialFwTag   (== FwTag for full image)
    #   16s InitVector
    #   28s Reserved
    auth_header = struct.pack(
        "<4sHHIII32s32s16s28s",
        SFUMAGIC,
        PROTOCOL_VERSION,
        fw_version,
        fw_size,
        0,          # PartialFwOffset
        0,          # PartialFwSize
        fw_tag,
        fw_tag,     # PartialFwTag == FwTag for a full image
        iv,
        b"\x00" * 28,  # Reserved
    )
    assert len(auth_header) == HEADER_AUTH_LEN, \
        f"Auth header size mismatch: {len(auth_header)} != {HEADER_AUTH_LEN}"

    # Step 5: ECDSA-P256/SHA-256 sign the 128-byte authenticated header.
    signature = _ecdsa_sign_raw(priv, auth_header)   # 64 bytes R||S
    assert len(signature) == HEADER_SIGN_LEN

    # Step 6: Assemble the full 320-byte header:
    #   [128 bytes authenticated] [64 bytes signature]
    #   [96 bytes FwImageState = 0xFF] [32 bytes PrevFingerprint = 0x00]
    fw_image_state   = b"\xFF" * HEADER_STATE_LEN   # VALID marker
    prev_fingerprint = b"\x00" * HEADER_FP_LEN      # first install

    header = auth_header + signature + fw_image_state + prev_fingerprint
    assert len(header) == HEADER_TOTAL_LEN, \
        f"Header size mismatch: {len(header)} != {HEADER_TOTAL_LEN}"

    # Step 7: Pad the header region out to SFU_IMG_IMAGE_OFFSET (0xFF =
    # erased-flash state) and append the clear firmware body.
    assert IMAGE_OFFSET >= HEADER_TOTAL_LEN, \
        f"IMAGE_OFFSET (0x{IMAGE_OFFSET:X}) must be >= header size (0x{HEADER_TOTAL_LEN:X})"
    pad = b"\xFF" * (IMAGE_OFFSET - HEADER_TOTAL_LEN)

    return header + pad + plaintext


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Sign a firmware binary for SBSFU "
                    "(ECCDSA_WITH_AES128_CBC_SHA256, clear body / NO_LOADER)."
    )
    parser.add_argument("--firmware",    required=True, help="Input raw .bin file.")
    parser.add_argument(
        "--private-key",
        default=str(default_keys_dir() / "ecdsa_private.pem"),
        help="ECDSA P-256 private key PEM "
             "(default: keys/ next to this script; dir override: $LIFU_BL_KEYS).",
    )
    parser.add_argument(
        "--aes-key",
        default=str(default_keys_dir() / "aes128.bin"),
        help="Raw 16-byte AES-128 key binary "
             "(default: keys/ next to this script; dir override: $LIFU_BL_KEYS).",
    )
    parser.add_argument(
        "--version", type=int, default=1,
        help="Firmware version number (1–65535, default: 1).",
    )
    parser.add_argument(
        "--output",
        help="Output signed image file (default: <firmware>_signed.bin).",
    )
    args = parser.parse_args()

    for label, p in (("private key", args.private_key), ("AES key", args.aes_key)):
        if not Path(p).is_file():
            sys.exit(
                f"ERROR: {label} not found: {p}\n"
                f"Place the keys in {default_keys_dir()}, point $LIFU_BL_KEYS at "
                f"the keys directory, or pass --private-key/--aes-key explicitly."
            )

    fw_path  = Path(args.firmware)
    out_path = Path(args.output) if args.output else fw_path.with_stem(fw_path.stem + "_signed")

    firmware_bin    = fw_path.read_bytes()
    private_key_pem = Path(args.private_key).read_bytes()
    aes_key         = Path(args.aes_key).read_bytes()

    print(f"[sign_firmware] Input        : {fw_path}  ({len(firmware_bin):,} bytes)")
    print(f"[sign_firmware] FW version   : {args.version}")

    signed = sign_firmware(
        firmware_bin    = firmware_bin,
        private_key_pem = private_key_pem,
        aes_key         = aes_key,
        fw_version      = args.version,
    )

    out_path.write_bytes(signed)

    header_size = HEADER_TOTAL_LEN
    fw_size     = len(signed) - IMAGE_OFFSET
    print(f"[sign_firmware] Header       : {header_size} bytes  (auth={HEADER_AUTH_LEN}, "
          f"sig={HEADER_SIGN_LEN}, state={HEADER_STATE_LEN}, fp={HEADER_FP_LEN})")
    print(f"[sign_firmware] FW @ offset  : 0x{IMAGE_OFFSET:X} (SFU_IMG_IMAGE_OFFSET; "
          f"0x{IMAGE_OFFSET - HEADER_TOTAL_LEN:X} bytes of 0xFF padding after header)")
    print(f"[sign_firmware] FW body      : {fw_size:,} bytes (clear)")
    print(f"[sign_firmware] Total output : {len(signed):,} bytes")
    print(f"[sign_firmware] Output       : {out_path}")
    print()
    print("Upload to device:")
    print(f"  dfu-util -D \"{out_path}\" -a 0 -s 0x08010000:leave")


if __name__ == "__main__":
    main()
