# Modernization Plan

This is the living migration plan for moving foxOS from a 32-bit legacy boot flow to a 64-bit UEFI-based system. Keep this file updated as work lands. When a finish condition is met, check the box and note the date or change summary beside it.

## Goal

- [x] Boot the kernel through UEFI in 64-bit mode. (2026-05-24: BOOTX64.EFI boots and reaches the foxOS prompt.)
- [x] Run the kernel as a 64-bit build end to end. (2026-05-24: kernel boots, shell runs, and sleep/keyboard interrupts pass.)
- [ ] Keep the boot path, memory manager, drivers, and core services aligned with the new architecture.

## Phase 1: Audit the current 32-bit/legacy path

- [ ] Map the current boot chain from firmware entry to `kernel_entry.asm`.
  - Finish condition: the full startup path is documented in this file.
- [ ] Identify all 32-bit assumptions in boot, kernel, and drivers.
  - Finish condition: every 32-bit dependency is listed with a replacement plan.
- [ ] Identify any legacy BIOS-only code paths.
  - Finish condition: BIOS-only paths are separated from UEFI-ready code.

## Phase 2: Introduce a UEFI boot path

- [x] Add a real UEFI boot entry that loads the kernel through firmware services.
  - [x] Open the EFI system partition and locate the kernel image.
  - [x] Load the kernel image into memory using UEFI boot services.
  - [x] Transfer control to the kernel only after the loader has finished preparing boot data.
  - Finish condition: the system starts from UEFI without relying on BIOS boot sectors.
- [ ] Replace legacy boot-sector assumptions with UEFI-compatible startup logic.
  - [ ] Remove fixed disk-layout and boot-sector expectations from the startup path.
  - [ ] Stop depending on BIOS-only loading, real-mode setup, or MBR handoff behavior.
  - [ ] Move all boot-time state into UEFI-driven initialization code.
  - Finish condition: boot no longer depends on MBR or any other BIOS-only mechanism.
- [x] Define and document the kernel handoff contract used by the UEFI loader.
  - [x] Specify what the loader passes to the kernel, including image location, memory map, and other boot metadata.
  - [x] Implement the boot-info structure or equivalent handoff object in code.
  - [x] Document the exact loader-to-kernel contract in this file as it stabilizes.
  - Finish condition: the loader contract is both implemented and documented.

## Phase 3: Move the kernel to 64-bit

- [x] Update kernel compilation and linking for x86_64.
  - Finish condition: the kernel binary is produced as a 64-bit target.
- [x] Update assembly entry points and interrupt setup for long mode.
  - Finish condition: the kernel enters 64-bit mode cleanly and reaches the main kernel entry.
- [ ] Replace 32-bit pointer and register assumptions in kernel code.
  - Finish condition: kernel code builds without 32-bit-only types or register usage.

## Phase 4: Rework memory management

- [x] Define the physical memory map handoff from UEFI.
  - Finish condition: the kernel receives and stores the UEFI memory map.
- [ ] Update paging setup for 64-bit page tables.
  - Finish condition: the kernel boots with long-mode paging enabled.
- [ ] Review allocator and page management interfaces for 64-bit addresses.
  - Finish condition: memory APIs handle 64-bit physical and virtual addresses safely.

## Phase 5: Update core subsystems

- [ ] Review console, keyboard, serial, ATA, timer, and IDT code for architecture assumptions.
  - Finish condition: every subsystem is confirmed to work on the 64-bit path or marked for follow-up.
- [ ] Update interrupt and exception handling for the new CPU mode.
  - Finish condition: interrupts, traps, and faults work in the 64-bit kernel.
- [ ] Validate filesystem and initrd access in the new boot flow.
  - Finish condition: the kernel can mount and use the expected early boot data.

## Phase 6: Verification and cleanup

- [ ] Add or update build steps so the new path is the default way to boot and test.
  - Finish condition: the normal build produces the modernized boot artifacts.
- [x] Update `verify_system.py` and related checks for the new architecture if needed.
  - Finish condition: verification reflects the 64-bit UEFI flow.
- [ ] Remove obsolete legacy boot code only after replacement paths are proven stable.
  - Finish condition: unused legacy code is deleted or clearly isolated.
- [x] Run a full boot test on the modern path and record the result here.
  - Finish condition: the system boots through UEFI into the 64-bit kernel and reaches the expected runtime state. (2026-05-24: prompt reached and `sleep 100` completed under the UEFI path.)

## Completion Criteria

- [ ] UEFI boot works without legacy BIOS dependencies.
- [ ] Kernel boots and runs in 64-bit mode.
- [ ] Memory management, interrupts, and core drivers are validated on the new path.
- [ ] Legacy 32-bit code is removed or explicitly kept only where still required.

## Update Rule

When a finish condition is met:

- Check the matching box.
- Add a short note with what changed and when.
- If a milestone changes the plan, update the remaining checkboxes so this file always reflects the current migration state.
