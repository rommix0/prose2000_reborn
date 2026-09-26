/* The DSP side of the board: the frame interrupt (IR0), the frame sender and the phoneme echo.
 *
 * Each 10 ms the DSP raises USF0 and pulses P0, and dsp_irq_service runs. dsp_frame_tick sends the frame built at
 * the previous interrupt, if there is one, and then builds the next from the track ring, so a frame reaches the DSP
 * one interrupt after it was built. With ESC[16N it then sends the phoneme log to the host, one phoneme per segment
 * played. */
#include "input.h"

unsigned dsp_status = 0x80;
int (*dsp_write_hook)(const uint16_t *frame, int words);
void (*frame_trace_hook)(unsigned pos, int mark, int alt, const uint8_t track[22]);

static frame_builder fb;

void dsp_load_rom(const prose_rom *rom)
{
	frame_builder_init(&fb, rom, 0);
}

/* D3261: send `words` words from DS:src to the data port, low byte first, each when the DSP shows RQM. Returns 1 if
 * the DSP was not asking (USF0 clear) or dropped USF0 within 10 polls of the last byte, 0 if it still asks. The C has
 * no DSP to poll: the hook takes the frame and returns 0 if it was not accepted. */
static int dsp_write_frame(int words, int src)
{
	uint16_t frame[40];
	if (!(dsp_status & 0x20))
		return 1;
	for (int i = 0; i < words; i++)
		frame[i] = ruw(src + 2 * i);
	if (dsp_write_hook && !dsp_write_hook(frame, words))
		return 0;
	dsp_status &= ~0x20u;
	return 1;
}

/* D8949: the frame buffer's owner. 3: may the builder start (no frame waiting, no build running)? 4: frame ready.
 * 5: is a frame ready? 6: frame taken. */
int dsp_frame_handshake(int op)
{
	switch (op) {
	case 3:
		if (rw(FRAME_READY))
			return 0;
		irq_disable_nested();
		if (rw(FRAME_BUILDING)) {
			irq_enable_nested();
			return 0;
		}
		ww(FRAME_BUILDING, 1);
		irq_enable_nested();
		return 1;
	case 4:
		ww(FRAME_READY, 1);
		ww(FRAME_BUILDING, 0);
		return 1;
	case 5:
		return rw(FRAME_READY);
	case 6:
		ww(FRAME_READY, 0);
		return 1;
	}
	fatal_error(0x22);
	return 0;
}

/* D89CB: the next frame from the track ring, if the generator is far enough ahead and the buffer is free: the frame's
 * segment mark (counted in SEGMENTS_PLAYED) and formant-coding bit are taken out of their bitsets, and frame_build
 * turns its 22 track bytes into the frame at DS:DBD2. The builder's state lives in DS (DBD2, DC22-DC26, the latch),
 * so the resets of the synthesis loop apply to it. */
void dsp_build_frame(void)
{
	uint8_t t[22];
	if (s16(rw(RING_READ) + rw(RING_LAG)) >= s16(rw(RING_LOW)))
		return;
	if (!dsp_frame_handshake(3))
		return;
	unsigned k = ruw(RING_READ) & 0x7F, byte = k >> 3, m = rb(BITMASK + 2 * (k & 7));
	int mark = (rb(RING_MARK + byte) & m) != 0;
	wb(RING_MARK + byte, rb(RING_MARK + byte) & ~m);
	int alt = (rb(RING_ALT + byte) & m) != 0;
	wb(RING_ALT + byte, rb(RING_ALT + byte) & ~m);
	for (int p = 0; p < 22; p++)
		t[p] = (uint8_t)rb(ruw(TRK_BASE(p)) + k);
	if (frame_trace_hook)
		frame_trace_hook(ruw(RING_READ), mark, alt, t);

	for (int i = 0; i < 40; i++)
		fb.w[i] = ruw(FRAME_BUF + 2 * i);
	fb.lfsr = ruw(FRAME_LFSR);
	fb.silent = s16(rw(FRAME_SILENT));
	fb.silent_run = s16(rw(FRAME_SILENT_RUN));
	fb.played = ruw(SEGMENTS_PLAYED);
	fb.latch = rb(LATCH);
	for (int i = 0; i < 15; i++)
		fb.work[i] = s16(rw(FRAME_WORK + 2 * i));
	frame_build(&fb, t, mark, alt);
	for (int i = 0; i < 22; i++)
		ww(FRAME_PARAMS + 2 * i, fb.p[i]);
	for (int i = 0; i < 15; i++)
		ww(FRAME_WORK + 2 * i, fb.work[i]);
	for (int i = 0; i < 40; i++)
		ww(FRAME_BUF + 2 * i, fb.w[i]);
	ww(FRAME_LFSR, fb.lfsr);
	ww(FRAME_SILENT, fb.silent);
	ww(FRAME_SILENT_RUN, fb.silent_run);
	ww(SEGMENTS_PLAYED, fb.played);
	if (fb.latch != rb(LATCH)) { /* the silence bits (D5D8B / D5D74) */
		wb(LATCH, fb.latch);
		wb(ruw(LATCH_PTR), fb.latch);
	}

	ww(RING_READ, rw(RING_READ) + 1);
	ww(FRAME_READY, 1);
	ww(FRAME_BUILDING, 0);
}

/* D64E1: send the phoneme log (written by paramgen_advance) to the host. Stress marks and punctuation (feature
 * +200:08) go at once; a phoneme (feature +0:80) costs one unit of budget, the segments played so far, after a lead-in
 * of two phonemes, so the echo keeps in step with the audio. The second lead-in phoneme also waits while playback is
 * held. Returns the budget left. */
static int phoneme_echo_drain(int budget)
{
	while (rw(ECHO_READ) != rw(ECHO_WRITE)) {
		int ch = rsb(ECHO_LOG + rw(ECHO_READ));
		int lead = s16(rw(ECHO_LEAD));
		if (lead != 0 && (lead != 1 || rw(PLAYBACK_WAIT)) && !(feature(ch, 0x200) & 8) && budget <= 0)
			break;
		if (!host_send(0, ch, 0, NULL))
			break;
		if (feature(ch, 0) & 0x80) {
			if (lead < 2)
				ww(ECHO_LEAD, lead + 1);
			else
				budget--;
		}
		ww(ECHO_READ, (rw(ECHO_READ) + 1) & 0x7F);
	}
	return budget;
}

/* D643C: send the ready frame unless output is held (ESC[H) or playback has reached the ring's hold point, then build
 * the next frame and, with the phoneme log on (N-flag 16), drain it. */
void dsp_frame_tick(void)
{
	ww(IRQ_NEST, rw(IRQ_NEST) + 1);
	latch_clear(8);
	if (!rw(OUTPUT_HOLD) && !rw(PLAYBACK_WAIT)) {
		if (!rw(FRAME_READY)) {
			ww(FRAME_MISSED, rw(FRAME_MISSED) + 1);
		} else {
			if (!dsp_write_frame(40, FRAME_BUF))
				fatal_error(0x24);
			ww(FRAME_READY, 0);
			if (rw(RING_READ) == rw(RING_HOLD))
				ww(PLAYBACK_WAIT, 1);
			if (rw(FRAME_MISSED))
				ww(FRAME_MISSED, 0);
		}
	}
	latch_set(8);
	ww(IRQ_NEST, rw(IRQ_NEST) - 1); /* then sti */
	int played = s16(rw(SEGMENTS_PLAYED));
	dsp_build_frame();
	if (rw(MODE) & 0x8000)
		ww(ECHO_BUDGET, phoneme_echo_drain(s16(rw(SEGMENTS_PLAYED)) - played + s16(rw(ECHO_BUDGET))));
}

/* D615F, IR0: latch bit 2 is high while it runs. A frame request (DSP status USF0) runs dsp_frame_tick; 20 interrupts
 * in a row without one are fatal (DSP timeout). */
void dsp_irq_service(void)
{
	latch_set(4);
	if (dsp_status & 0x20) {
		ww(DSP_SPURIOUS, 0);
		dsp_frame_tick();
	} else {
		ww(DSP_SPURIOUS, rw(DSP_SPURIOUS) + 1);
		if (s16(rw(DSP_SPURIOUS)) >= 20)
			fatal_error(0x23);
	}
	latch_clear(4);
}
