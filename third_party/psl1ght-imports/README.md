PSL1GHT common import templates, MIT license; upstream source:
https://github.com/ps3dev/PSL1GHT/tree/master/ppu/sprx/common

Local changes: retain metadata with used attributes; remove a duplicate const;
use explicit ATTRIBUTE_PRXPTR pointers with a 44-byte header assertion.
The metadata C file is compiled with -fno-toplevel-reorder so the zero-length
stub-table marker remains before its entries. Assembly uses -mregnames.
AAC NIDs are derived with the PS3 export-name SHA1 scheme, checked against
known installed vdec NID vectors. The finished ELF is checked for exact NID,
stub, and trampoline correspondence, not just successful linking.
ABI field facts were cross-checked with public RPCS3 headers; no emulator
implementation or Cell SDK header/binary is included.
