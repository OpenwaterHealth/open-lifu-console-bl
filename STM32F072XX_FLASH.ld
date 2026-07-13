/*
******************************************************************************
**  File        : STM32F072xx_FLASH.ld
**  Abstract    : SBSFU secure bootloader linker script for STM32F072xB
**                (Cortex-M0, 128K flash / 16K RAM). Single-image build:
**                the Secure Engine core is linked into this ELF, with the
**                call gate / key block / SE startup pinned at the fixed
**                addresses defined in Linker/mapping_sbsfu.ld.
**
**                IMPORTANT: LTO must stay disabled — the SE_IF and SE data
**                placement below matches by object-file name, which -flto
**                destroys (and the call gate resets the MCU when callers sit
**                outside the SE_IF region).
******************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack (end of 16K RAM) */
_estack = 0x20004000;

_Min_Heap_Size  = 0x0;    /* no malloc use in the bootloader */
_Min_Stack_Size = 0x800;  /* SBSFU scatter-verify chunk buffers run on this stack;
                             the CMOX ECC working buffer is a static in SE RAM */

INCLUDE mapping_fwimg.ld
INCLUDE mapping_sbsfu.ld

MEMORY
{
  ISR_VECTOR (rx) : ORIGIN = __ICFEDIT_intvec_start__, LENGTH = VECTOR_SIZE
  /* remaining regions come from mapping_sbsfu.ld */
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >ISR_VECTOR

  /* SE call gate at fixed address: 4 pad bytes keep SE_CallGate() at
   * region base + 4; callers use SE_CALLGATE_REGION_ROM_START + 5 (thumb). */
  .SE_CallGate_Code :
  {
    BYTE(0);
    BYTE(0);
    BYTE(0);
    BYTE(0);
    KEEP(*(.SE_CallGate_Code))
    FILL(0xFF);
  } >SE_Entry_Secure_ROM_Region

  /* SE key functions (se_key.s), fixed location */
  .SE_Key_Data :
  {
    KEEP(*(.SE_Key_Data))
    FILL(0xFF);
  } >SE_Key_region_ROM

  /* SE_CORE_Startup at fixed address, called via SE_STARTUP_REGION_ROM_START+1.
   * Nothing references it by symbol across the fixed-address call, so KEEP is
   * mandatory against --gc-sections. Object name differs per build system. */
  .SE_Startup_Code :
  {
    KEEP(*se_startup.o(.text .text*))
    KEEP(*se_startup.c.obj(.text .text*))
    FILL(0xFF);
  } >SE_Startup_region_ROM

  /* SE interface shims: MUST be inside [SE_IF region] — SE_CallGate() issues
   * NVIC_SystemReset() when its caller's return address is outside this
   * region (IS_CALLER_SE_IF). GCC has no per-file section pragma, so place
   * by object name (matches both .o and CMake .c.obj). */
  .SE_IF_Code :
  {
    . = ALIGN(4);
    *(.SE_Entry_Secure_Code)
    *(.SE_IF_Code)
    *se_interface_bootloader*(.text .text*)
    *se_interface_common*(.text .text*)
    *se_interface_application*(.text .text*)
    FILL(0xFF);
    . = ALIGN(4);
  } >SE_IF_region_ROM

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >SB_ROM_region

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >SB_ROM_region

  .ARM.extab :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >SB_ROM_region

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >SB_ROM_region

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >SB_ROM_region

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >SB_ROM_region

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >SB_ROM_region

  /* SE-owned statics (key buffers etc.) live in the dedicated SE RAM region,
   * initialized by SE_CORE_Startup() -> __gcc_data_init() (data_init.c, which
   * uses the _se_* symbols) and wiped by SFU_SE_RAM_Erase() before app launch
   * and DFU entry. Placed by object name BEFORE the generic .data/.bss rules
   * (first match wins). */
  _se_sidata = LOADADDR(.se_data);
  .se_data :
  {
    . = ALIGN(4);
    _se_sdata = .;
    *se_callgate*(.data .data*)
    *se_crypto_bootloader*(.data .data*)
    *se_crypto_common*(.data .data*)
    *se_fwimg*(.data .data*)
    *se_utils*(.data .data*)
    *se_exception*(.data .data*)
    *se_low_level*(.data .data*)
    . = ALIGN(4);
    _se_edata = .;
  } >SE_RAM_region AT>SB_ROM_region

  .se_bss (NOLOAD) :
  {
    . = ALIGN(4);
    _se_sbss = .;
    *se_callgate*(.bss .bss* COMMON)
    *se_crypto_bootloader*(.bss .bss* COMMON)
    *se_crypto_common*(.bss .bss* COMMON)
    *se_fwimg*(.bss .bss* COMMON)
    *se_utils*(.bss .bss* COMMON)
    *se_exception*(.bss .bss* COMMON)
    *se_low_level*(.bss .bss* COMMON)
    . = ALIGN(4);
    _se_ebss = .;
  } >SE_RAM_region

  /* Initialized data (copied from flash by Reset_Handler) */
  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >SB_RAM_region AT>SB_ROM_region

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >SB_RAM_region

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >SB_RAM_region

  /* NOTE: unlike the H7 reference, libc/libm/libgcc are NOT discarded here —
   * Cortex-M0 has no divide instruction and requires libgcc's __aeabi_* helpers,
   * and newlib-nano provides memcpy/memset used throughout. */

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
