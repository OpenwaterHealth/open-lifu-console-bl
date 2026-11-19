/**
  ******************************************************************************
  * @file    bl_info.c
  * @brief   Bootloader info block: the flash layout, where the application can
  *          read it.
  *
  * Placed at BL_INFO_ADDRESS (0x080000E0), in the padding after the vector
  * table (STM32F072XX_FLASH.ld, .isr_vector). The application reads it to
  * find the preset page, and writes presets only when the block says page 61
  * is outside the slot: bootloaders before 1.1.0-rc.2 have no block, and their
  * slot still covers page 61, so a preset written there would invalidate the
  * installed image. The block is constant: it describes the layout this
  * bootloader was built with (Core/Inc/memory_map.h).
  ******************************************************************************
  */
#include "memory_map.h"

__attribute__((section(".bl_info"), used))
const bl_info_t g_bl_info =
{
  .magic       = BL_INFO_MAGIC,
  .layout      = BL_INFO_LAYOUT,
  .reserved    = 0U,
  .slot_base   = MEM_SLOT1_BASE,
  .slot_end    = MEM_SLOT1_END,
  .preset_base = MEM_PRESET_BASE,
  .preset_size = MEM_PRESET_SIZE,
  .caps        = BL_CAPS,
  .reserved2   = 0U,
};

_Static_assert(sizeof(bl_info_t) == 32U, "bl_info_t is part of the app contract: 32 bytes");
_Static_assert(MEM_PRESET_BASE == MEM_SLOT1_END, "the preset page sits right above the slot");
_Static_assert(MEM_PRESET_END == MEM_APP_CONFIG_BASE, "the preset page ends where the app config starts");
_Static_assert(MEM_APP_CONFIG_BASE + MEM_APP_CONFIG_SIZE == MEM_ANTIROLLBACK_BASE, "config then anti-rollback");
_Static_assert(MEM_ANTIROLLBACK_BASE + MEM_ANTIROLLBACK_SIZE == MEM_FLASH_END, "anti-rollback is the last page");
