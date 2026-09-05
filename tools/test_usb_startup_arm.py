#!/usr/bin/env python3
"""Offline startup and USB clock/IRQ wiring checks, not a silicon emulator.

Execute the linked Reset_Handler/SystemInit up to main, then separately run
the USB clock and timer setup with modeled reset/clock register aliases.
No device is opened. Register writes prove software intent, not clock lock,
electrical signaling, real elapsed time, or NVIC interrupt delivery.
"""
import argparse

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK

from test_usb_arm import UsbArm, RAM

TIMER3 = 0x40029000

class StartupArm(UsbArm):
    def __init__(self, elf_path):
        super().__init__(elf_path)
        # Synthetic invalid factory trims exercise the SDK fallback path;
        # these are not captured values from the keyboard's factory region.
        self.cpu.mem_map(0x0009F000, 0x1000)
        self.writes = []
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.peripheral_write,
                          begin=0x40000000, end=0x400FFFFF)

    def peripheral_write(self, cpu, access, address, size, value, _):
        self.writes.append((address, value))
        if address == 0x40034000:  # FLASH CMD_SET_READ_MODE during clock setup
            assert value == 2, "unexpected flash command in USB bring-up"
            self.put32(0x40034FE0, 4)  # model command-completed status
        # SYSCON PRESETCTRL and AHBCLKCTRL SET/CLR aliases.
        for base in (0x40000100, 0x40000200):
            for offset, setting in ((0x20, True), (0x40, False)):
                if base + offset <= address < base + offset + 12:
                    target = address - offset
                    old = self.u32(target)
                    self.put32(target, old | value if setting else old & ~value)

    def startup(self, elf_path):
        # Do not preinitialize .data/.bss: load the updater image at its LMA
        # and require the actual startup instructions to do the copying.
        self.cpu.mem_write(0x04000000, b"\xa5" * 0x8000)
        self.cpu.mem_write(RAM, b"\xa5" * 0x4000)
        expected_data = None
        with open(elf_path, "rb") as stream:
            elf = ELFFile(stream)
            for segment in elf.iter_segments():
                if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]:
                    self.cpu.mem_write(segment["p_paddr"], segment.data())
            expected_data = elf.get_section_by_name(".data").data()
        reset = self.u32(0x20000004)
        self.cpu.reg_write(UC_ARM_REG_SP, self.u32(0x20000000))
        self.cpu.emu_start(reset, self.symbols["main"] & ~1, count=1000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == self.symbols["main"] & ~1
        assert self.cpu.reg_read(UC_ARM_REG_SP) == 0x04008000
        assert self.cpu.reg_read(UC_ARM_REG_PRIMASK) == 0
        assert self.u32(0xE000ED08) == 0x20000000
        assert self.u32(0xE000ED88) & 0xF0000F == 0xF0000F
        data = self.symbols["__data_start__"]
        assert bytes(self.cpu.mem_read(data, len(expected_data))) == expected_data
        for start, end in (("__bss_start__", "__bss_end__"),
                           ("__HeapBase", "__HeapLimit"),
                           ("__StackLimit", "__StackTop")):
            size = self.symbols[end] - self.symbols[start]
            assert bytes(self.cpu.mem_read(self.symbols[start], size)) == bytes(size), start
        for vector, name in ((15, "SysTick_Handler"), (16 + 13, "CTIMER3_IRQHandler"),
                             (16 + 47, "USB1_IRQHandler")):
            assert self.u32(0x20000000 + 4 * vector) == self.symbols[name] | 1
        print("PASS actual Reset_Handler/SystemInit: data copy, BSS/heap/stack, VTOR, vectors")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    args = parser.parse_args()
    dev = StartupArm(args.elf)
    dev.startup(args.elf)
    dev.call("board_init")
    assert dev.u32(dev.symbols["SystemCoreClock"]) == 96000000
    assert dev.u32(0x40000280) == 3 and dev.u32(0x40000284) == 0
    assert dev.u32(0x40000380) & 0xFF == 0  # AHB divider = 1
    assert dev.u32(0xE000E014) == 95999  # SysTick reload, 1 ms at 96 MHz
    print("PASS core clock selection and SysTick setup (synthetic factory trims, modeled flash command completion)")
    for name in ("board_usb_clock_init", "usb_errata_init", "board_usb_isr_enable"):
        dev.stubs.discard(dev.symbols[name] & ~1)
    dev.call("board_usb_clock_init")
    assert bytes(dev.cpu.mem_read(RAM, 0x4000)) == bytes(0x4000)
    assert dev.u32(0x400A3050) == 0x90000
    assert (dev.u32(0x400380A0) >> 22) & 7 == 6  # PLL multiplier 30, 16 MHz input
    assert (0x400380A4, 0x1000) in dev.writes  # PLL power enable
    assert (0x400380A4, 0x40) in dev.writes  # PLL USB clocks enable
    assert dev.u32(0x40038000) == 0  # USB PHY PWD
    # Production reset IDs: 0x10019, 0x20011, 0x20010, 0x20004,
    # 0x20005, 0x20007, 0x20006 (FUN_2001aab4).
    observed_resets = [(a, v) for a, v in dev.writes if 0x40000120 <= a < 0x4000012C]
    assert observed_resets == [(0x40000124, 1 << 25)] + [
        (0x40000128, 1 << bit) for bit in (17, 16, 4, 5, 7, 6)], observed_resets
    print("PASS USB peripheral reset order, PORTMODE=0x90000, dedicated RAM clear")
    dev.call("usb_errata_init")
    assert dev.u32(0x40000278) == 3  # FRO_HF -> CTIMER3
    assert dev.u32(TIMER3 + 0x18) == 95999  # CTIMER3 MR0
    assert dev.u32(0xE000E100) & (1 << 13)
    dev.call("board_usb_isr_enable")
    assert dev.u32(0xE000E104) & (1 << 15)
    assert bytes(dev.cpu.mem_read(0xE000E400 + 13, 1)) == b"\x00"
    assert bytes(dev.cpu.mem_read(0xE000E400 + 47, 1)) == b"\x20"
    # Exercise the linked vector -> SDK driver -> registered timer callback.
    dev.put32(dev.symbols["s_timer_ticks"], 1)
    dev.put32(TIMER3, 1)  # match 0 pending
    dev.call("CTIMER3_IRQHandler")
    assert dev.u32(dev.symbols["s_timer_ticks"]) == 0
    print("PASS CTIMER3 match, IRQ enable/priority, vector-to-callback dispatch; USB IRQ priority")


if __name__ == "__main__":
    main()
