/*
 * Checks the v1.1 C functions against RAM snapshots captured from v1.1 in the emulator (native/ with the DSP stub; see
 * REFERENCE §16), like stage_replay does for v3.4.1.
 *
 *   v1_replay CAPTURE.bin [FUNC]
 *
 * For each record it loads the RAM before the call, runs the C function with the captured arguments, and compares
 * all of RAM except the stack with the RAM after the call. Captures made with interrupts running (NO_CLI) come with
 * CAPTURE.bin.isr, which lists for each interrupted call the bytes the interrupts changed; those are not compared.
 */
#include "capture.h"
#include "v1.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf fatal_jmp;
static void on_fatal(int code)
{
	(void)code;
	longjmp(fatal_jmp, 1);
}
static void on_restart(int code) { (void)code; }

/* v1.1 uses near calls: the arguments follow the return address (stack[0]) */
// clang-format off
static int run(const record *r, int *ret, int *has_ret)
{
	const uint16_t *arg = r->stack + 1;
	int a0 = (int16_t)arg[0], a1 = (int16_t)arg[1], a2 = (int16_t)arg[2], a3 = (int16_t)arg[3];
	*has_ret = 1;
	switch (r->func) {
	case 0xF5513: *ret = v1_input_ring_free(); break;
	case 0xF553E: *ret = v1_input_getc(); break;
	case 0xF55DF: *ret = v1_input_put(a0); break;
	case 0xF5655: *ret = v1_output_ring_free(); break;
	case 0xF5680: *ret = v1_tx_next_byte(); *has_ret = 2; break; /* returns AL only */
	case 0xF5765: { uint8_t p[16]; for (int i = 0; i < 16; i++) p[i] = rb((uint16_t)arg[3] + i); *ret = v1_host_send(a0, a1, a2, p); break; }
	case 0xEC417: *ret = (int)v1_node_insert(arg[0], a1, a2, arg[3] & 0xFF); break;
	case 0xEC3B4: *ret = (int)v1_node_append(a0, a1 & 0xFF); break;
	case 0xEC4F1: *ret = (int)v1_node_prev(arg[0]); break;
	case 0xEC735: *ret = (int)v1_node_next(arg[0]); break;
	case 0xEC658: *ret = (int)v1_node_free(arg[0], a1); break;
	case 0xEC512: *ret = v1_stage_commit(); break;
	case 0xEC917: *ret = v1_stage_accepts(arg[0]); break;
	case 0xF12F5: *ret = v1_command_apply(); break;
	case 0xEC9D6: *ret = v1_stage_begin(arg[0]); break;
	case 0xF53A0: *ret = v1_stage_input_run(); break;
	case 0xECACC: *ret = v1_stage_playback_run(); break;
	case 0xF716E: *ret = v1_loop_reset(); break;
	case 0xECBAC: *ret = v1_stage_text_rules_run(); break;
	case 0xF5A7B: *ret = v1_stage_lexical_run(); break;
	case 0xF5C12: *ret = v1_affixes(); break;
	case 0xF5E49: *ret = (int)v1_affix_match(arg[0], arg[1], arg[2], a3); break;
	case 0xF5F40: *ret = v1_stem_repair(); break;
	case 0xF669D: *ret = v1_rule_flags_ok(arg[0]); break;
	case 0xF66E6: *ret = v1_rule_letters_ok(arg[0]); break;
	case 0xF673A: *ret = v1_context_match(arg[0], arg[1], a2); break;
	case 0xF6B9F: *ret = v1_lex_lookup(); break;
	case 0xEEB9C: *ret = v1_stage_prosody_run(); break;
	case 0xF1659: *ret = v1_stage_paramgen_run(); break;
	case 0xEE49E: *ret = v1_frame_handshake(a0); break;
	case 0xF52A3: *ret = v1_pg_mul15(a0, a1); break;
	case 0xF26A1: *ret = v1_pg_scale(a0, a1); break;
	case 0xF1559: *ret = v1_pg_ring(a0, a1); break;
	case 0xF2520: *ret = (int)v1_pg_insert_silence(arg[0], a1); break;
	case 0xF27DB: *ret = (int)v1_pg_next_segment(); break;
	case 0xF1B0E: *ret = v1_pg_hold((int8_t)a0, a1, a2, a3); break;
	case 0xF1A38: *ret = v1_pg_hold_run(); break;
	case 0xEF393: *ret = (int)v1_pr_prev_ph(arg[0]); break;
	case 0xEF453: *ret = (int)v1_pr_next_sym(arg[0]); break;
	case 0xEF3D4: *ret = v1_pr_test(arg[0], a1, a2); break;
	case 0xEF4F4: *ret = v1_pr_scan(a0, a1, a2, a3, (int16_t)arg[4]); break;
	case 0xEED9F: *ret = (int)v1_pr_phrase_scan(); break;
	case 0xF0DA0: *ret = (int)v1_pr_stress_digit(arg[0]); break;
	case 0xEF161: *ret = v1_pr_phrase_walk(); break;
	case 0xEEC6E: *ret = v1_pr_cursor_advance(); break;
	case 0xF0082: *ret = v1_pr_duration_rules(); break;
	default:
		*has_ret = 0;
		switch (r->func) {
		case 0xEE1BB: v1_host_rx_char(a0 & 0xFF, a1); break;
		case 0xEDB32: v1_host_escape_parser(); break;
		case 0xEE564: v1_frame_build(); break;
		case 0xEDA11: v1_frame_tick(); break;
		case 0xEE3A3: v1_tx_send(); break;
		case 0xED99C: v1_irq_disable_nested(); break;
		case 0xEDB16: v1_irq_enable_nested(); break;
		case 0xEC0FB: v1_latch_set(a0); break;
		case 0xEC0E6: v1_latch_clear(a0); break;
		case 0xEC328: v1_uart_tx_start(); break;
		case 0xEC393: v1_uart_tx_stop(); break;
		case 0xEC290: v1_uart_command_set(a0); break;
		case 0xEC27B: v1_uart_command_clear(a0); break;
		case 0xEC783: v1_nodes_reset(); break;
		case 0xF5346: v1_host_rings_reset(); break;
		case 0xF6069: v1_lts_rules(); break;
		case 0xF6A5C: v1_vowel_finish(a0); break;
		case 0xEF488: v1_pr_sentence_reset(); break;
		case 0xEF29A: v1_pr_skip_commands(); break;
		case 0xEF663: v1_pr_given_values(a0); break;
		case 0xF11BC: v1_pr_context_load(); break;
		case 0xF0F5F: v1_pr_word_reduce(a0, (int8_t)a1); break;
		case 0xF0D15: v1_pr_mark_syllable(arg[0]); break;
		case 0xEF6F6: v1_pr_allophones(); break;
		case 0xEFFC1: v1_pr_merge_geminate(); break;
		case 0xF050A: v1_pr_duration_set(); break;
		case 0xF08F6: v1_pr_vowel_context(); break;
		case 0xF0A45: v1_pr_f0_set(); break;
		case 0xF0E6A: v1_pr_time_segment(); break;
		case 0xF51F0: v1_pg_ramp(arg[0], arg[1], a2, a3, (int16_t)arg[4], (int16_t)arg[5]); break;
		case 0xF52B4: v1_pg_blend_back(arg[0], arg[1], a2, a3, (int16_t)arg[4]); break;
		case 0xF52FD: v1_pg_blend_fwd(arg[0], arg[1], a2, a3, (int16_t)arg[4]); break;
		case 0xF36DF: v1_pg_fill(arg[0], arg[1], a2, a3); break;
		case 0xF3708: v1_pg_adjust_back(arg[0], arg[1], a2, a3, (int16_t)arg[4]); break;
		case 0xF34D5: v1_pg_emit(a0); break;
		case 0xF3418: v1_pg_pinch(arg[0], arg[1], arg[2], a3, (int16_t)arg[4]); break;
		case 0xF2601: v1_pg_state_reset(); break;
		case 0xF1D06: v1_pg_load_targets(); break;
		case 0xF387D: v1_pg_setup(); break;
		case 0xF3983: v1_pg_voiced(); break;
		case 0xF3782: v1_pg_aspirate_back(); break;
		case 0xF3A32: v1_pg_voiceless(); break;
		case 0xF3ACA: v1_pg_burst_frame(); break;
		case 0xF3AEE: v1_pg_after_stop(); break;
		case 0xF4F17: v1_pg_release(); break;
		case 0xF3B9A: v1_pg_vowel_start(); break;
		case 0xF3DD9: v1_pg_vowel(); break;
		case 0xF45CE: v1_pg_glide(); break;
		case 0xF485D: v1_pg_consonant(); break;
		case 0xF4B9C: v1_pg_fricative(); break;
		case 0xF4C4B: v1_pg_stop(); break;
		case 0xF4D00: v1_pg_closure(); break;
		case 0xF4EA4: v1_pg_nasal(); break;
		case 0xF2A0A: v1_pg_loci(); break;
		case 0xF2DC5: v1_pg_loci_special(); break;
		case 0xF2FDC: v1_pg_finish(); break;
		case 0xF3208: v1_pg_amp_onsets(); break;
		case 0xF21BF: v1_pg_segment(); break;
		case 0xF2709: v1_pg_advance(); break;
		default: return 0;
		}
	}
	return 1;
}
// clang-format on

typedef struct {
	uint32_t func;
	long runs, bad, fixed;
} tally;

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: v1_replay CAPTURE.bin [FUNC]\n");
		return 2;
	}
	uint32_t only = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 16) : 0;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	char isr_name[1024];
	snprintf(isr_name, sizeof isr_name, "%s.isr", argv[1]);
	FILE *isr = fopen(isr_name, "r");
	long isr_next = -1;
	if (isr && fscanf(isr, "%ld", &isr_next) != 1)
		isr_next = -1;
	static uint8_t skip[0x3000];
	v1_load_rom();
	v1_fatal_hook = on_fatal;
	v1_restart_hook = on_restart;
	static record r;
	static uint8_t got[0x3000];
	tally t[64] = {0};
	int nt = 0, total_bad = 0;
	long index = 0;
	while (read_record(f, &r)) {
		static int ret, has_ret, k;
		ret = 0;
		index++;
		memset(skip, 0, sizeof skip);
		if (isr_next == index - 1) { /* the interrupts' bytes: the rest of the line */
			unsigned a;
			int c;
			while ((c = fgetc(isr)) != EOF && c != '\n')
				if (c != ' ' && ungetc(c, isr) != EOF && fscanf(isr, "%x", &a) == 1 && a < 0x3000)
					skip[a] = 1;
			if (fscanf(isr, "%ld", &isr_next) != 1)
				isr_next = -1;
		}
		if (only && r.func != only)
			continue;
		for (k = 0; k < nt && t[k].func != r.func; k++)
			;
		if (k == nt && nt < 64)
			t[nt++].func = r.func;
		v1_load_ram(r.before);
		static long rejects;
		rejects = v1_lex_bound_rejects;
		if (setjmp(fatal_jmp) == 0) {
			if (!run(&r, &ret, &has_ret))
				continue;
		}
		t[k].runs++;
		v1_save_ram(got);
		unsigned lo = (r.sp0 - 0x400u) & 0xFFFFu, hi = r.sp0 + 0x10u;
		unsigned mask = has_ret == 2 ? 0xFF : 0xFFFF;
		int bad = has_ret && ((unsigned)ret & mask) != (r.ax & mask), shown = 0;
		/* the C fixed the lexicon lookup's index bug here, so it need not match the firmware (v1_lex_lookup) */
		int fixed = v1_lex_bound_rejects != rejects;
		for (unsigned a = 0; a < 0x3000 && !fixed; a++) {
			unsigned ds = a + V1_DS_RAM_LO;
			if ((ds >= lo && ds < hi) || skip[a])
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
		if (bad && !fixed && !t[k].bad && has_ret && ((unsigned)ret & mask) != (r.ax & mask))
			printf("record %ld (%05X): returned %04X want %04X\n", index, r.func, (uint16_t)ret, r.ax);
		if (fixed)
			t[k].fixed++;
		else if (bad) {
			t[k].bad++;
			total_bad++;
		}
	}
	fclose(f);
	for (int k = 0; k < nt; k++)
		if (t[k].runs)
			printf("%05X: %ld runs, %ld differ%s\n", t[k].func, t[k].runs, t[k].bad,
			       t[k].fixed ? " (plus the lexicon index bug, fixed)" : "");
	return total_bad != 0;
}
