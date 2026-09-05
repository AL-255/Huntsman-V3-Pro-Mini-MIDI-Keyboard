ENTRY(Reset_Handler)

MEMORY
{
    APP_IMAGE (rx)  : ORIGIN = 0x20000000, LENGTH = 0x00020000
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

    /DISCARD/ : { *(.note*) *(.comment*) }
}

ASSERT(__app_load_end__ <= ORIGIN(APP_IMAGE) + LENGTH(APP_IMAGE), "application exceeds 128 KiB updater slot")
ASSERT(__bss_end__ <= ORIGIN(SRAMX) + LENGTH(SRAMX), "application SRAMX data exceeds 24 KiB")
ASSERT(__usb_sram_end__ <= ORIGIN(USB_SRAM) + LENGTH(USB_SRAM), "USB data exceeds dedicated USB SRAM")
