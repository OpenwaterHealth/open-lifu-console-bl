/*
*****************************************************************************
**  File        : mapping_sbsfu.ld
**  Abstract    : SBSFU memory region mapping for STM32F072xB (Cortex-M0)
**                Single-image build: SE core is linked into the bootloader
**                ELF (no separate SECoreBin binary). The F072 has no MPU and
**                no PCROP, so binary isolation buys nothing; the SE call
**                gate, key block and startup entry are still pinned at fixed
**                addresses so the ST Secure Engine address-based dispatch
**                (SE_CALLGATE_REGION_ROM_START + 5, SE_STARTUP + 1) and the
**                IS_CALLER_SE_IF() bounds check work unchanged.
**
**                Flash: 128K (64 x 2K pages)   RAM: 16K
**                Bootloader occupies pages 0-19 (40K), read-only over DFU.
*****************************************************************************
*/

/* Cortex-M0: 48 vectors = 0xC0; padded to 0x100 */
VECTOR_SIZE = 0x100;

__ICFEDIT_intvec_start__ = 0x08000000;

/* SE call gate: SE_CallGate() sits at region base + 4 (4 pad bytes), called
 * via SE_CALLGATE_REGION_ROM_START + 5 (thumb) — see se_interface_common.h */
__ICFEDIT_SE_CallGate_region_ROM_start__   = 0x08000100;
__ICFEDIT_SE_CallGate_region_ROM_end__     = 0x080001FF;

/* SE key block (se_key.s: SE_ReadKey_1 / SE_ReadKey_1_Pub, Thumb-1 LDR-literal
 * encoding for Cortex-M0). Fixed so the key location is stable across builds. */
__ICFEDIT_SE_Key_region_ROM_start__        = 0x08000200;
__ICFEDIT_SE_Key_region_ROM_end__          = 0x080003FF;

/* SE startup: SE_CORE_Startup() called from SBSFU via fixed address + 1 */
__ICFEDIT_SE_Startup_region_ROM_start__    = 0x08000400;
__ICFEDIT_SE_Startup_region_ROM_end__      = 0x080004FF;

/* Nominal SE code region = the pinned SE block (call gate + key + startup).
 * The rest of the SE core (se_callgate, se_crypto, se_fwimg, uECC, sha256)
 * flows into the common SB code region below — with no MPU there is no
 * enforceable boundary, and a fixed-size region would only add overflow risk. */
__ICFEDIT_SE_Code_region_ROM_start__       = __ICFEDIT_SE_CallGate_region_ROM_start__;
__ICFEDIT_SE_Code_nokey_region_ROM_start__ = __ICFEDIT_SE_Startup_region_ROM_start__;
__ICFEDIT_SE_Code_region_ROM_end__         = __ICFEDIT_SE_Startup_region_ROM_end__;

/* SE Interface: the se_interface_*.c marshalling shims MUST live in a bounded
 * region — SE_CallGate() resets the MCU if its caller's return address is
 * outside [SE_IF_region_ROM] (IS_CALLER_SE_IF check). Placed by object name;
 * this is why LTO must stay OFF for this target. */
__ICFEDIT_SE_IF_region_ROM_start__         = 0x08000500;
__ICFEDIT_SE_IF_region_ROM_end__           = 0x08000EFF;

/* HDP is an STM32H7 feature; regions kept as zero-size symbols only so
 * mapping_export.h externs resolve if ever referenced. */
__ICFEDIT_SB_HDP_region_ROM_start__        = 0x08000F00;
__ICFEDIT_SB_HDP_region_ROM_end__          = 0x08000EFF; /* empty */

/* SBSFU + SE core + HAL + USB DFU + CMOX cryptolib code region.
 * Bootloader occupies the first 64K of flash (pages 0-31); the ST CMOX
 * cryptographic library pulls in more code than the software crypto it
 * replaced, so the region is 64K (was 40K). See Core/Inc/memory_map.h. */
__ICFEDIT_SB_region_ROM_start__            = 0x08000F00;
__ICFEDIT_SB_region_ROM_end__              = 0x0800FFFF;

SE_Entry_Secure_ROM_Region_Length = __ICFEDIT_SE_CallGate_region_ROM_end__ - __ICFEDIT_SE_CallGate_region_ROM_start__ + 1;
SE_Key_region_ROM_Length          = __ICFEDIT_SE_Key_region_ROM_end__ - __ICFEDIT_SE_Key_region_ROM_start__ + 1;
SE_Startup_region_ROM_Length      = __ICFEDIT_SE_Startup_region_ROM_end__ - __ICFEDIT_SE_Startup_region_ROM_start__ + 1;
SE_IF_region_ROM_Length           = __ICFEDIT_SE_IF_region_ROM_end__ - __ICFEDIT_SE_IF_region_ROM_start__ + 1;
SB_ROM_region_Length              = __ICFEDIT_SB_region_ROM_end__ - __ICFEDIT_SB_region_ROM_start__ + 1;

/* RAM (16K total: 0x20000000 - 0x20003FFF)
 * SE RAM region first: holds SE .data/.bss including the CMOX ECDSA working
 * buffer (~2000 bytes) and the SHA-256 handle; 4K reserved. Wiped by
 * SFU_SE_RAM_Erase() before app launch and before DFU entry. */
__ICFEDIT_SE_region_RAM_start__     = 0x20000000;
__ICFEDIT_SE_region_RAM_stack_top__ = 0x20000100; /* SE stack unused in the no-MPU model */
__ICFEDIT_SE_region_RAM_end__       = 0x20000FFF;

/* HDP code RAM: H7-only, empty on F0 */
__ICFEDIT_SB_HDP_Code_region_RAM_start__ = 0x20001000;
__ICFEDIT_SB_HDP_Code_region_RAM_end__   = 0x20000FFF; /* empty */

/* SBSFU RAM region (12K: SB .data/.bss + main stack) */
__ICFEDIT_SB_region_RAM_start__     = 0x20001000;
__ICFEDIT_SB_region_RAM_end__       = 0x20003FFF;

SE_RAM_region_Length = __ICFEDIT_SE_region_RAM_end__ - __ICFEDIT_SE_region_RAM_stack_top__ + 1;
SB_RAM_region_Length = __ICFEDIT_SB_region_RAM_end__ - __ICFEDIT_SB_region_RAM_start__ + 1;

MEMORY
{
 SE_Entry_Secure_ROM_Region (rx) : ORIGIN = __ICFEDIT_SE_CallGate_region_ROM_start__, LENGTH = SE_Entry_Secure_ROM_Region_Length
 SE_Key_region_ROM (rx)          : ORIGIN = __ICFEDIT_SE_Key_region_ROM_start__,      LENGTH = SE_Key_region_ROM_Length
 SE_Startup_region_ROM (rx)      : ORIGIN = __ICFEDIT_SE_Startup_region_ROM_start__,  LENGTH = SE_Startup_region_ROM_Length
 SE_IF_region_ROM (rx)           : ORIGIN = __ICFEDIT_SE_IF_region_ROM_start__,       LENGTH = SE_IF_region_ROM_Length
 SB_ROM_region (rx)              : ORIGIN = __ICFEDIT_SB_region_ROM_start__,          LENGTH = SB_ROM_region_Length
 SE_RAM_region (xrw)             : ORIGIN = __ICFEDIT_SE_region_RAM_stack_top__,      LENGTH = SE_RAM_region_Length
 SB_RAM_region (xrw)             : ORIGIN = __ICFEDIT_SB_region_RAM_start__,          LENGTH = SB_RAM_region_Length
}
