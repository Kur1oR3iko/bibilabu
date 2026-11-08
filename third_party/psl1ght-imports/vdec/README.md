# Extended AVC imports

Uses the existing PSL1GHT open-source import stubs and library header (license in parent directory). Standard NIDs match ps3dev/PSL1GHT ppu/sprx/libvdec/exports.h. Ex NIDs are derived from cellVdecQueryAttrEx / cellVdecOpenEx using the PS3 SHA-1 NID algorithm, cross-checked against the standard exports.

Wire structures are described in RPCS3 cellVdec.h and independently used by Yo decode-h264.c. Only ABI facts are used here. Build-time checks cover table mapping and structure offsets; runtime checks require both imports to resolve. This is not proof that firmware supports portrait 720p.
