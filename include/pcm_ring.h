// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
// Producer publishes its new counter only after write success. Storage is
// reclaimed at hardware consumption, not when copied into the output port.
int wili_pcm_ring_write(float*,unsigned capacity,uint32_t produced,uint32_t consumed,const float*,unsigned frames,unsigned slots);
int wili_pcm_ring_read(const float*,unsigned capacity,uint32_t queued,uint32_t produced,float*,unsigned frames);
// Real cellAdec float PCM can contain NaN or slight >1 peaks; ring_write
// rejects those by design. Repair in place and report the first raw bit
// pattern. Returns the number of repaired channel values.
unsigned wili_pcm_sanitize(float *samples,unsigned count,uint32_t *first_bad_bits);
