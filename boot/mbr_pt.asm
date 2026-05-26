; Minimal MBR sector with a single partition table entry.
; NOTE: This MBR has no boot code (non-bootable). It only defines a partition table.
; The first partition starts at PT_LBA_START and spans PT_LBA_COUNT sectors.

%ifndef PT_LBA_START
%define PT_LBA_START 2048
%endif

%ifndef PT_LBA_COUNT
%define PT_LBA_COUNT (32*1024*1024/512 - PT_LBA_START) ; default for 32MiB image
%endif

BITS 16
ORG 0x7C00

; 446 bytes of zeroed boot code (no code => not bootable)
times 446 db 0

; Partition table (4 entries x 16 bytes)
; Entry 0: bootable, type 0x83, CHS fields dummies, LBA start/count from macros
; Status
DB 0x80
; CHS start (dummy values)
DB 0x00, 0x02, 0x00
; Type (Linux)
DB 0x83
; CHS end (dummy values)
DB 0xFF, 0xFF, 0xFF
; LBA start (little-endian)
DD PT_LBA_START
; LBA count (little-endian)
DD PT_LBA_COUNT

; Remaining 3 entries zeroed
times (3*16) db 0

; Signature
DW 0xAA55
