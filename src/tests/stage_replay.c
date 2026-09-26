/*
 * Checks the C pipeline stages (serial receive and escape parser, input and host_send, text rules, lexical, prosody,
 * parameter generator, playback) against RAM snapshots taken around firmware functions in the emulator.
 *
 *   stage_replay CAPTURE.bin [FUNC]
 *
 * A capture record (written by a scratch harness on native/, not committed) holds the function's linear address,
 * SS:SP and 8 stack words at entry, the 12 KB of RAM before the call, AX and the RAM bytes that changed by the
 * return. Interrupts were held off during the stage's step, so every change comes from the code under test.
 * The test loads the RAM into the C data segment, runs the C function and compares all of RAM except the stack.
 */
#include "capture.h"
#include "input.h"
#include "lexical.h"
#include "pg.h"
#include "prosody.h"
#include "textrules.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf fatal_jmp;
static void on_fatal(int code)
{
	printf("  fatal_error(0x%X)\n", code);
	longjmp(fatal_jmp, 1);
}

// clang-format off: one line per function reads better here
/* Runs the C version of r->func. Returns 0 if there is none; sets *ret for functions that return a value. */
static int run(const record *r, int *ret, int *has_ret)
{
	const uint16_t *arg = r->stack + (r->far ? 2 : 1);
	const int16_t a0 = (int16_t)arg[0], a1 = (int16_t)arg[1], a2 = (int16_t)arg[2];
	*has_ret = 1;
	switch (r->func) {
	/* input stage (DE09D) */
	case 0xDE09D: *ret = stage_input_run(); break;
	case 0xDE2C5: *ret = input_getc(); break;
	case 0xDE2AF: *ret = input_ring_free(); break;
	case 0xD35C4: *ret = node_append(a0, a1); break;
	case 0xDE398: *ret = output_ring_free(); break;
	case 0xDE345: *ret = input_put(a0); break;
	case 0xDE3AE: *ret = tx_next_byte(); break;
	case 0xD8949: *ret = dsp_frame_handshake(a0); break;
	case 0xE38FF: *ret = loop_reset(); break;
	case 0xDE4C6: { int p[16]; for (int i = 0; i < 16; i++) p[i] = rb((uint16_t)arg[3] + i); *ret = host_send(a0, a1, a2, p); break; }
	/* text-rules stage (D41A4) */
	case 0xD41A4: *ret = stage_text_rules_run(); break;
	case 0xD4B18: *ret = text_rules_commit(a0); break;
	case 0xD4C3D: *ret = match_list((int8_t)a0, a1); break;
	case 0xD4CDB: *ret = char_test(a0, a1); break;
	case 0xD4D76: *ret = not_stage_command(a0); break;
	/* lexical stage (E4A81) */
	case 0xE4A81: *ret = stage_lexical_run(); break;
	case 0xE4A8A: *ret = lexical_step(); break;
	case 0xE4AFB: *ret = finish_span(); break;
	case 0xE4CAB: *ret = scan_ahead(); break;
	case 0xE4D16: *ret = take_word(); break;
	case 0xE3DBF: *ret = allophone_rules(a0); break;
	case 0xD3225: *ret = lex_index_word(a0, a1); break;
	case 0xD3245: *ret = lex_read_byte((uint16_t)a0); break;
	case 0xD738A: *ret = lex_lookup(); break;
	case 0xE4F57: *ret = lex_lookup_with_affixes(); break;
	case 0xE5152: *ret = affix_match(a0, a1, a2, (int16_t)arg[3]); break;
	case 0xE5264: *ret = stem_respell_and_lookup(a0); break;
	case 0xD80F3: *ret = lts_class_match(a0); break;
	case 0xD8165: *ret = lts_letters_match(a0); break;
	case 0xD81AB: *ret = match_context_pattern((uint16_t)a0, a1, a2); break;
	case 0xD3614: *ret = node_insert(a0, a1, a2, (int16_t)arg[3]); break;
	case 0xD3805: *ret = node_delete(a0, a1); break;
	case 0xD38B8: *ret = node_next(a0); break;
	case 0xD36D2: *ret = node_prev(a0); break;
	case 0xD36FB: *ret = stage_commit(); break;
	case 0xD40F4: *ret = stage_playback_run(); break;
	/* prosody (D9274) */
	case 0xD9274: *ret = prosody_run(); break;
	case 0xD9281: *ret = prosody_step(); break;
	case 0xD3B66: *ret = stage_window_update(a0); break;
	case 0xD9336: *ret = prosody_walk(); break;
	case 0xD9492: *ret = prosody_scan_phrase(); break;
	case 0xD9945: *ret = prev_symbol(a0); break;
	case 0xD997E: *ret = node_has(a0, a1, a2); break;
	case 0xD99F2: *ret = next_symbol(a0); break;
	case 0xD9A9D: *ret = context_search((int8_t)a0, a1, a2, (int16_t)arg[3], (int8_t)arg[4]); break;
	case 0xD9D2B: *ret = stage_run_command(); break;
	case 0xDA9DC: *ret = duration_rules(); break;
	default: *has_ret = 0; break;
	}
	if (*has_ret)
		return 1;
	switch (r->func) {
	case 0xD4B81: text_rules_emit(a0, a1); break;
	case 0xD713F: host_rx_char(a0 & 0xFF, a1); break;
	case 0xD734B: tx_send(); break;
	case 0xD643C: dsp_status = 0xA0; dsp_frame_tick(); break; /* called when the DSP asks */
	case 0xD89CB: dsp_build_frame(); break;
	case 0xE3CE7: quit_reset(); break;
	case 0xD3900: node_list_init(); break;
	case 0xD658C: host_escape_parser(); break;
	case 0xD6D8C: host_status_report(a0); break;
	case 0xD63E5: irq_disable_nested(); break;
	case 0xD6576: irq_enable_nested(); break;
	case 0xD5D8B: latch_set(a0); break;
	case 0xD5D74: latch_clear(a0); break;
	case 0xD62FE: uart_tx_start(); break;
	case 0xD63C6: uart_command_set(a0); break;
	case 0xD63AF: uart_command_clear(a0); break;
	case 0xD4DB0: phoneme_spelling(); break;
	case 0xE4C8D: take_span(); break;
	case 0xE4E90: word_stress_marks(); break;
	case 0xE546F: mark_stressed_syllable(a0); break;
	case 0xE5542: function_word_rules(); break;
	case 0xE5736: word_boundary(); break;
	case 0xD78AE: lts_rules(); break;
	case 0xD8551: vowel_reduce(a0); break;
	case 0xD9A20: prosody_time_segment(); break;
	case 0xD9BE6: given_values(a0); break;
	case 0xD9F9A: context_load(); break;
	case 0xDA101: phrase_breaks((int8_t)a0); break;
	case 0xDA770: insert_phrase_break(a0, (int8_t)a1, a2); break;
	case 0xDA811: mark_break_neighbour(a0, a1); break;
	case 0xDA8AB: merge_geminate(); break;
	case 0xDB4A6: klatt_duration(); break;
	case 0xDBA35: accent_context(); break;
	case 0xDBC51: accent_reset(); break;
	case 0xDBC84: f0_target(); break;
	case 0xDC1EA: segment_prosody(); break;
	case 0xDC332: insert_pause(a0); break;
	case 0xDC57B: cluster_duration(); break;
	/* parameter generator */
	case 0xDCB00: *ret = paramgen_run(); *has_ret = 1; break;
	case 0xDCB0B: *ret = paramgen_step(); *has_ret = 1; break;
	case 0xDD703: paramgen_segment(); break;
	case 0xDD1C0: paramgen_load_targets(); break;
	case 0xDEB3A: paramgen_segment_setup(); break;
	case 0xDE96E: paramgen_apply_rules(); break;
	case 0xD3D2F: param_emit_segment((int16_t)arg[0]); break;
	case 0xDDC70: paramgen_advance(); break;
	case 0xDDE1F: *ret = find_segment_end(); *has_ret = 1; break;
	case 0xDCEEC: *ret = paramgen_hold_step(); *has_ret = 1; break;
	case 0xD3FFB: pg_shift_aspiration(); break;
	case 0xDEBFA: pg_voiceless_onset(); break;
	case 0xDEC87: pg_after_closure(); break;
	case 0xDEE85: pg_sonorant_onset(); break;
	case 0xDF245: pg_vowel(); break;
	case 0xDFB5B: pg_sonorant_consonant(); break;
	case 0xDFE87: pg_obstruent_voicing(); break;
	case 0xE01C0: pg_fricative_amps(); break;
	case 0xE067F: pg_closure_types(); break;
	case 0xE08EA: pg_stop_burst(); break;
	case 0xE1024: pg_locus_weights(); break;
	case 0xE1A49: pg_consonant_loci(); break;
	case 0xE1C1A: pg_apply_loci(); break;
	case 0xE2535: pg_amplitude_boundaries(); break;
	case 0xE2FB8: pg_finalize(); break;
	case 0xE30C1: pg_release_onset(); break;
	default: return 0;
	}
	return 1;
}

// clang-format on

typedef struct {
	uint32_t func;
	long runs, bad;
} tally;

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: stage_replay CAPTURE.bin [FUNC]\n");
		return 2;
	}
	static prose_rom rom;
	prose_rom_builtin(&rom);
	uint32_t only = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 16) : 0;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	pg_load_rom(&rom);
	lexical_load_rom(&rom);
	dsp_load_rom(&rom);
	pg_fatal_hook = on_fatal;
	pg_host_send_hook = host_send;
	static record r;
	static uint8_t got[0x3000];
	tally t[64] = {0};
	int nt = 0, total_bad = 0;
	long index = 0;
	while (read_record(f, &r)) {
		static int ret, has_ret, k; /* static: they must survive the longjmp from on_fatal */
		ret = 0;
		index++;
		if (only && r.func != only)
			continue;
		for (k = 0; k < nt && t[k].func != r.func; k++)
			;
		if (k == nt && nt < 64)
			t[nt++].func = r.func;
		pg_load_ram(r.before);
		if (setjmp(fatal_jmp) == 0) {
			if (!run(&r, &ret, &has_ret))
				continue;
		}
		t[k].runs++;
		pg_save_ram(got);
		/* the stack (SS = DS = F410) is not compared */
		unsigned lo = (r.sp0 - 0x400u) & 0xFFFFu, hi = r.sp0 + 0x10u;
		int bad = has_ret && (uint16_t)ret != r.ax, shown = 0;
		for (unsigned a = 0; a < 0x3000; a++) {
			unsigned ds = a + DS_RAM_LO;
			if (ds >= lo && ds < hi)
				continue;
			if (got[a] != r.after[a]) {
				if (!bad && !t[k].bad)
					printf("record %ld (%05X):\n", index, r.func);
				if (!t[k].bad && shown++ < 24)
					printf("  DS:%04X got %02X want %02X (was %02X)\n", ds, got[a], r.after[a],
					       r.before[a]);
				bad = 1;
			}
		}
		if (bad && !t[k].bad && has_ret && (uint16_t)ret != r.ax)
			printf("record %ld (%05X): returned %04X want %04X\n", index, r.func, (uint16_t)ret, r.ax);
		if (bad) {
			t[k].bad++;
			total_bad++;
		}
	}
	fclose(f);
	for (int k = 0; k < nt; k++)
		if (t[k].runs)
			printf("%05X: %ld runs, %ld differ\n", t[k].func, t[k].runs, t[k].bad);
	return total_bad != 0;
}
