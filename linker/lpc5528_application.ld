ENTRY(Reset_Handler)

MEMORY
{
    APP_IMAGE (rx)  : ORIGIN = 0x20000000, LENGTH = 0x0001FC00
    /* Image addresses, NOT physical flash addresses. Keep stock storage intact. */
    APP_CONFIG (r)  : ORIGIN = 0x2001FC00, LENGTH = 0x00000400
    SRAMX     (rwx) : ORIGIN = 0x04000000, LENGTH = 0x00006000
    STACK     (rwx) : ORIGIN = 0x04006000, LENGTH = 0x00002000
    USB_SRAM  (rwx) : ORIGIN = 0x40100000, LENGTH = 0x00004000
}

SECTIONS
{
    .isr_vector ORIGIN(APP_IMAGE) : ALIGN(4)
    {
        KEEP(*(.isr_vector))
    } > APP_IMAGE

    .text : ALIGN(4)
    {
        /* Retain the future MIDI API for offline transfer tests. */
        KEEP(*(.text.usb_midi_send))
        *(.text*)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        __text_end__ = .;
    } > APP_IMAGE

    .ARM.extab : ALIGN(4) { *(.ARM.extab* .gnu.linkonce.armextab.*) } > APP_IMAGE
    .ARM.exidx : ALIGN(4)
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > APP_IMAGE

    /* APP_IMAGE is SRAM at runtime, not memory-mapped flash. Calibration's
     * independent per-key registers are initialized by keyboard_live_init.
     * Include their bytes in the ordinary updater image/load bound. */
    .calibration_state : ALIGN(4)
    {
        *(.calibration_state)
    } > APP_IMAGE

    .data : ALIGN(4)
    {
        __data_start__ = .;
        *(.data*)
        . = ALIGN(4);
        __data_end__ = .;
    } > SRAMX AT > APP_IMAGE
    __etext = LOADADDR(.data);
    __app_load_end__ = LOADADDR(.data) + SIZEOF(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > SRAMX

    .heap (NOLOAD) : ALIGN(8)
    {
        __HeapBase = .;
        . += 0x400;
        __HeapLimit = .;
    } > SRAMX

    .usb_sram (NOLOAD) : ALIGN(64)
    {
        __usb_sram_start__ = .;
        *(m_usb_bdt*)
        *(m_usb_global*)
        *(.usb_sram*)
        . = ALIGN(64);
        __usb_sram_end__ = .;
    } > USB_SRAM

    __StackLimit = ORIGIN(STACK);
    __StackTop = ORIGIN(STACK) + LENGTH(STACK);
    PROVIDE(__stack = __StackTop);

    /* Two 512-byte slots. The updater binary's existing FF padding initializes
     * these bytes; startup must neither clear them nor copy data over them.
     * Physical erase/program remains disabled pending backing-flash mapping
     * and bootloader integrity verification. */
    .app_config (NOLOAD) :
    {
        __app_config_start__ = .;
        __app_config_slot_a__ = .;
        . += 0x200;
        __app_config_slot_b__ = .;
        . += 0x200;
        __app_config_end__ = .;
    } > APP_CONFIG

    /DISCARD/ : { *(.note*) *(.comment*) }
}

ASSERT(__app_load_end__ <= ORIGIN(APP_IMAGE) + LENGTH(APP_IMAGE), "application overlaps reserved configuration slots")
ASSERT(__app_config_start__ == 0x2001FC00 && __app_config_end__ == 0x20020000, "invalid application configuration reservation")
ASSERT(__bss_end__ <= ORIGIN(SRAMX) + LENGTH(SRAMX), "application SRAMX data exceeds 24 KiB")
ASSERT(__usb_sram_end__ <= ORIGIN(USB_SRAM) + LENGTH(USB_SRAM), "USB data exceeds dedicated USB SRAM")
