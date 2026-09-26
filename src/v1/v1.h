/*
 * Prose 2000 v1.1 (1983): the C decompilation's memory model (REFERENCE §16).
 *
 * v1.1 is a separate, older program from v3.4.1, built instead of it with -DPROSE_VERSION=1. Like v3.4.1 it keeps
 * all of its state in its data segment, here DS = F766: DS:0000-899F is ROM (linear F7660-FFFFF; the data proper
 * ends at DS:8712), DS:89A0-B99F is the 12 KB SRAM (linear 00000-02FFF), and the I/O page follows. The C works on
 * an image of that segment. Writes outside the RAM window are dropped, as on the board. The ROM part comes from the
 * extracted data (src/data/prose_v1_data.c), so no ROM files are needed.
 *
 * Addresses in comments are linear (code segment EC00: linear = EC000 + the offset Ghidra shows in
 * prose2k_v11_cs.bin). All arithmetic is 16-bit as on the 8086.
 */
#ifndef V1_H
#define V1_H

#include <stddef.h>
#include <stdint.h>

extern uint8_t v1_ds[0x10000];

#define V1_DS_RAM_LO 0x89A0u /* DS:89A0 = linear 00000 */
#define V1_DS_RAM_HI 0xB9A0u /* DS:B9A0 = linear 03000 (I/O) */

void v1_load_rom(void);                        /* DS:0000-899F from the extracted data */
void v1_load_ram(const uint8_t ram[0x3000]);  /* DS:89A0-B99F from a RAM snapshot */
void v1_save_ram(uint8_t ram[0x3000]);

static inline unsigned rb(unsigned a) { return v1_ds[a & 0xFFFFu]; }
static inline int rsb(unsigned a) { return (int8_t)v1_ds[a & 0xFFFFu]; }
static inline int rw(unsigned a) { return (int16_t)(v1_ds[a & 0xFFFFu] | v1_ds[(a + 1) & 0xFFFFu] << 8); }
static inline unsigned ruw(unsigned a) { return (uint16_t)rw(a); }
static inline void wb(unsigned a, int v)
{
	a &= 0xFFFFu;
	if (a >= V1_DS_RAM_LO && a < V1_DS_RAM_HI)
		v1_ds[a] = (uint8_t)v;
}
static inline void ww(unsigned a, int v)
{
	wb(a, v);
	wb(a + 1, v >> 8);
}
static inline int s16(int v) { return (int16_t)v; } /* wrap an intermediate to 16 bits */

/* 32-bit event counters (a low word, then a high word), which v1.1 keeps for most events */
static inline void count32(unsigned a)
{
	unsigned lo = ruw(a) + 1;
	ww(a, lo);
	if ((lo & 0xFFFF) == 0)
		ww(a + 2, rw(a + 2) + 1);
}

/* fatal_error (ED9B6): counts the error, keeps its code and restarts the synthesis loop; the hook must not return */
extern void (*v1_fatal_hook)(int code);
void v1_fatal_error(int code);

/* ---- board (v1_board.c) ---- */
#define V1_PIC_MASK 0x8CA0  /* byte: copy of the 8259 mask */
#define V1_LATCH 0x8CA1     /* byte: copy of the peripheral latch (port 3401) */
#define V1_UART_CMD 0x8CA2  /* byte: copy of the 8251 command register */
#define V1_DSP_SPURIOUS 0x8CA3 /* byte: DSP interrupts in a row without a frame request */
#define V1_POWERED_UP 0x8CA4
#define V1_FREE_COUNT 0x8CA8 /* free nodes */
#define V1_IRQ_NEST 0xA56E  /* nesting count of v1_irq_disable_nested */

extern unsigned v1_dip_port;    /* the DIP switch port as the board returns it (0 = on) */
extern unsigned v1_uart_status; /* the 8251 status port; bit 7 = DSR */

int v1_dip_switch_test(int mask); /* EC0CE */
void v1_latch_set(int bits);      /* EC0FB */
void v1_latch_clear(int bits);    /* EC0E6 */
void v1_irq_disable(void);        /* EC10F */
void v1_irq_enable(void);         /* EC11D */
void v1_uart_command_set(int bits);   /* EC290 */
void v1_uart_command_clear(int bits); /* EC27B */
int v1_uart_status_test(int mask);    /* EC2A3 */
void v1_uart_tx_start(void);      /* EC328 */
void v1_uart_tx_stop(void);       /* EC393 */
void v1_irq_disable_nested(void); /* ED99C */
void v1_irq_enable_nested(void);  /* EDB16 */

/* ---- host link (v1_host.c) ---- */
#define V1_INPUT_RING 0xA252   /* 256 bytes */
#define V1_INPUT_READ 0xA352
#define V1_INPUT_WRITE 0xA354
#define V1_INPUT_AHEAD 0xA356  /* a byte read ahead by the input stage, or -1 */
#define V1_INPUT_XOFF 0xA358   /* XOFF has been sent */
#define V1_OUTPUT_RING 0xA35A  /* 128 bytes */
#define V1_OUTPUT_READ 0xA3DA
#define V1_OUTPUT_WRITE 0xA3DC
#define V1_FLOW_PENDING 0xA3DE /* byte: XON (1) / XOFF (2) queued for the transmitter (DIP S4-2) */
#define V1_TX_STOPPED 0xA3E0   /* the host sent XOFF */
#define V1_TX_RUNNING 0xA3E2
#define V1_TX_PENDING 0xA3E4
#define V1_BEL_COUNT 0xA3E6    /* byte */
#define V1_SELFTEST_PTR 0xA3E8 /* next byte of the self-test text (DIP S4-7) */
#define V1_HOST_MODE 0xA57A    /* the N flags (bit n-1 = flag n; 0x200 = N10, BEL replies) */
#define V1_OUTPUT_HOLD 0x95C2  /* ESC[H: no frames go to the DSP */

extern void (*v1_tx_hook)(int c); /* each byte the transmitter sends */

int v1_input_ring_free(void);  /* F5513 */
int v1_input_getc(void);       /* F553E */
int v1_input_put(int c);       /* F55DF */
int v1_output_ring_free(void); /* F5655 */
int v1_host_send(int kind, int ch, int count, const uint8_t *params); /* F5765 */
void v1_host_rings_reset(void); /* F5346 */
int v1_tx_next_byte(void);     /* F5680 */
int v1_tx_send(void);          /* EE3A3 */
void v1_uart_tx_isr(void);     /* EC341 */

/* ---- receive side and escape parser (v1_escape.c) ---- */
#define V1_RX_RING 0xA23E      /* 16 bytes */
#define V1_RX_READ 0xA24E
#define V1_RX_WRITE 0xA250
#define V1_RX_BUSY 0x9598      /* the parser is draining the ring */
#define V1_ESC_STATE 0x9596    /* 1 text, 2 after ESC, 3 in the parameters */
#define V1_ESC_INDEX 0x959C    /* byte: the parameter being read */
#define V1_ESC_OVERFLOW 0x959E /* a value above 255 */
#define V1_ESC_PARAMS 0x95A0   /* 16 words, -1 = not given */
#define V1_STOP_REQUEST 0xA568 /* ESC[S */
#define V1_END_REQUEST 0xA56A  /* ESC[x */
#define V1_SET_INPUT 0xA570    /* ESC[I */
#define V1_SET_WORD 0xA572     /* ESC[P */
#define V1_SET_RATE 0xA574     /* ESC[r */
#define V1_SET_PITCH 0xA576    /* ESC[p */
#define V1_SET_AMPLITUDE 0xA578 /* ESC[a */
#define V1_L_TABLE 0xA57C      /* 18 bytes set by ESC[p0;p1 l */

/* ^R and ESC[nT restart the synthesis loop (loop_restart EC021) and do not return on the board. With a hook the
 * parser stops after calling it; without one it carries on. */
extern void (*v1_restart_hook)(int code);

void v1_host_rx_char(int ch, int status); /* EE1BB */
void v1_host_escape_parser(void);         /* EDB32 */

/* ---- node list and stage windows (v1_nodes.c) ----
 * A node is 8 bytes: next, prev, a flags word (bits 0-2 the kind, bits 3-7 flags, high byte the first command value),
 * the second command value and the character. Kinds as in v3.4.1: 0 command, 1 input text, 2 text, 3 symbol, 4 timed
 * segment, 5 hold, 6 free, 7 sentinel. */
#define V1_NODE_NEXT 0
#define V1_NODE_PREV 2
#define V1_NODE_FLAGS 4
#define V1_NODE_ARG0 5 /* = the high byte of the flags word */
#define V1_NODE_ARG1 6
#define V1_NODE_CH 7
#define V1_NODE_POOL 0x8D5E /* 250 nodes */
#define V1_NODE_COUNT 250
#define V1_STAGE 0x8CA6      /* the current stage record, or 0 */
#define V1_LIST_END 0x8D36   /* -> the list's end sentinel (8D3A); the input stage appends in front of it */
#define V1_LIST_START 0x8D38 /* -> the start sentinel (8D42) */
#define V1_FREE_END 0x8D4A   /* -> the free list's end sentinel (8D4E) */
#define V1_FREE_START 0x8D4C /* -> its start sentinel (8D56) */

/* stage records, 0x1C bytes each (v3.4.1: 0x22), one per stage after the input stage; a record's next stage's
 * record follows it */
#define V1_REC_TEXTRULES 0x8CAA
#define V1_REC_LEXICAL 0x8CC6
#define V1_REC_PROSODY 0x8CE2
#define V1_REC_PARAMGEN 0x8CFE
#define V1_REC_PLAYBACK 0x8D1A
#define V1_REC_SIZE 0x1C
#define V1_REC_FIRST 0x00  /* the window: first node */
#define V1_REC_DONE 0x02   /* the first node not finished (0 = all) */
#define V1_REC_CURSOR 0x04
#define V1_REC_AHEAD 0x06  /* look-ahead cursor */
#define V1_REC_LAST 0x08   /* the window's last node */
#define V1_REC_INPUT 0x0E  /* the stage's copies of the host settings (ESC[I, P, r, p, a, N) */
#define V1_REC_WORD 0x10
#define V1_REC_RATE 0x12
#define V1_REC_PITCH 0x14
#define V1_REC_AMPLITUDE 0x16
#define V1_REC_MODE 0x18
#define V1_REC_MASK 0x1A   /* node kinds the stage works on (v1_stage_accepts) */
#define V1_STOP_INDEX 0xA56C /* the index marker that raised the stop request (0 for ESC[x) */
#define V1_L_VALUES 0xA1F2   /* the ESC[l values as the stages receive them */

void v1_nodes_reset(void);                                   /* EC783 */
unsigned v1_node_insert(unsigned ref, int where, int kind, int ch); /* EC417 */
unsigned v1_node_append(int kind, int ch);                   /* EC3B4 */
unsigned v1_node_prev(unsigned node);                        /* EC4F1 */
unsigned v1_node_next(unsigned node);                        /* EC735 */
unsigned v1_node_free(unsigned node, int dir);               /* EC658 */
int v1_stage_commit(void);                                   /* EC512 */
int v1_stage_accepts(unsigned node);                         /* EC917 */
int v1_command_apply(void);                                  /* F12F5 */
int v1_stage_begin(unsigned rec);                            /* EC9D6 */

/* ---- input and playback stages (v1_input.c) ---- */
#define V1_SEGMENTS_PLAYED 0x96F6 /* timed segments the frame builder has finished, for the playback stage */

int v1_stage_input_run(void);    /* F53A0 */
int v1_stage_playback_run(void); /* ECACC */

/* ---- text-rules stage (v1_textrules.c) ---- */
#define V1_TR_PROGRAM 0x057E /* the rule program; goto / call targets are relative to it */
#define V1_TR_PC 0x952E      /* program counter */
#define V1_TR_TEST 0x9530    /* 1: the program counter is at a condition, 0: in an action list */
#define V1_TR_YIELDED 0x9532 /* the yield action has returned to the synthesis loop once */
#define V1_TR_REPL 0x9534    /* the replacement text after the pattern match_list matched */
#define V1_TR_COUNT 0x9536   /* byte: bytes left in the current action list */
#define V1_TR_SKIP 0x9538    /* byte: bytes to skip when it is done */
#define V1_TR_STACK 0x953A   /* call stack: 20 entries of 4 bytes (pc, count, skip) */
#define V1_TR_CALLS 0x958A   /* call stack pointer (it is full when it reaches its own address) */
#define V1_TR_REG 0x958C     /* byte registers of the rule program */

int v1_stage_text_rules_run(void); /* ECBAC */

/* ---- lexical stage (v1_lexical.c) ---- */
int v1_stage_lexical_run(void);                                           /* F5A7B */
int v1_affixes(void);                                                     /* F5C12 */
unsigned v1_affix_match(unsigned p1, unsigned p2, unsigned list, int dir); /* F5E49 */
int v1_stem_repair(void);                                                 /* F5F40 */
void v1_lts_rules(void);                                                  /* F6069 */
int v1_rule_flags_ok(unsigned rule);                                      /* F669D */
int v1_rule_letters_ok(unsigned rule);                                    /* F66E6 */
int v1_context_match(unsigned pat, unsigned node, int dir);               /* F673A */
void v1_vowel_finish(int final);                                          /* F6A5C */
int v1_lex_lookup(void);                                                  /* F6B9F */
extern long v1_lex_bound_rejects; /* lookups the C refuses where the firmware reads past its index (a firmware bug) */

/* ---- prosody stage (v1_prosody.c) ---- */
int v1_stage_prosody_run(void);                                    /* EEB9C */
unsigned v1_pr_prev_ph(unsigned n);                                /* EF393 */
unsigned v1_pr_next_sym(unsigned n);                               /* EF453 */
int v1_pr_test(unsigned n, int mask, int neg);                     /* EF3D4 */
int v1_pr_scan(int dir, int count, int yes, int no, int mode);     /* EF4F4 */
void v1_pr_sentence_reset(void);                                   /* EF488 */
void v1_pr_skip_commands(void);                                    /* EF29A */
void v1_pr_given_values(int restore);                              /* EF663 */
void v1_pr_context_load(void);                                     /* F11BC */
void v1_pr_word_reduce(int end, int c);                            /* F0F5F */
unsigned v1_pr_phrase_scan(void);                                  /* EED9F */
void v1_pr_mark_syllable(unsigned n);                              /* F0D15 */
unsigned v1_pr_stress_digit(unsigned n);                           /* F0DA0 */
int v1_pr_phrase_walk(void);                                       /* EF161 */
int v1_pr_cursor_advance(void);                                    /* EEC6E */
void v1_pr_allophones(void);                                       /* EF6F6 */
void v1_pr_merge_geminate(void);                                   /* EFFC1 */
int v1_pr_duration_rules(void);                                    /* F0082 */
void v1_pr_duration_set(void);                                     /* F050A */
void v1_pr_vowel_context(void);                                    /* F08F6 */
void v1_pr_f0_set(void);                                           /* F0A45 */
void v1_pr_time_segment(void);                                     /* F0E6A */

/* ---- parameter generator (v1_paramgen.c) ---- */
int v1_pg_mul15(int a, int b);                                         /* F52A3 */
void v1_pg_ramp(unsigned trk, unsigned pos, int k, int len, int from, int to); /* F51F0 */
void v1_pg_blend_back(unsigned trk, unsigned pos, int k, int gap, int v); /* F52B4 */
void v1_pg_blend_fwd(unsigned trk, unsigned pos, int k, int len, int v); /* F52FD */
void v1_pg_fill(unsigned trk, unsigned pos, int n, int v);             /* F36DF */
void v1_pg_adjust_back(unsigned trk, unsigned pos, int k, int n, int d); /* F3708 */
int v1_pg_scale(int p, int v);                                         /* F26A1 */
void v1_pg_state_reset(void);                                          /* F2601 */
int v1_pg_ring(int op, int n);                                         /* F1559 */
void v1_pg_emit(int p);                                                /* F34D5 */
void v1_pg_pinch(unsigned a, unsigned b, unsigned pos, int n, int w);  /* F3418 */
void v1_pg_load_targets(void);                                         /* F1D06 */
void v1_pg_setup(void);                                                /* F387D */
void v1_pg_voiced(void);                                               /* F3983 */
void v1_pg_aspirate_back(void);                                        /* F3782 */
void v1_pg_voiceless(void);                                            /* F3A32 */
void v1_pg_burst_frame(void);                                          /* F3ACA */
void v1_pg_after_stop(void);                                           /* F3AEE */
void v1_pg_release(void);                                              /* F4F17 */
void v1_pg_vowel_start(void);                                          /* F3B9A */
void v1_pg_vowel(void);                                                /* F3DD9 */
void v1_pg_glide(void);                                                /* F45CE */
void v1_pg_consonant(void);                                            /* F485D */
void v1_pg_fricative(void);                                            /* F4B9C */
void v1_pg_stop(void);                                                 /* F4C4B */
void v1_pg_closure(void);                                              /* F4D00 */
void v1_pg_nasal(void);                                                /* F4EA4 */
void v1_pg_loci(void);                                                 /* F2A0A */
void v1_pg_loci_special(void);                                         /* F2DC5 */
void v1_pg_finish(void);                                               /* F2FDC */
void v1_pg_amp_onsets(void);                                           /* F3208 */
void v1_pg_segment(void);                                              /* F21BF */
unsigned v1_pg_insert_silence(unsigned n, int kind);                   /* F2520 */
unsigned v1_pg_next_segment(void);                                     /* F27DB */
void v1_pg_advance(void);                                              /* F2709 */
int v1_pg_hold(int ch, int n, int room, int first);                    /* F1B0E */
int v1_pg_hold_run(void);                                              /* F1A38 */
int v1_stage_paramgen_run(void);                                       /* F1659 */

/* ---- frame builder and frame interrupt (v1_frame.c) ---- */
extern unsigned v1_dsp_status; /* the DSP's status byte: 0x20 (USF0) = wants a frame, 0x80 = RQM */
/* each frame sent to the DSP (37 words), after the words have gone through v1_dsp_write_hook */
extern void (*v1_frame_hook)(const uint16_t *frame, int words);
/* Not in the firmware (for the DLL): each frame's 18 track bytes (p0-p17) as the frame builder takes them, and the
   phoneme of each timed segment the playback stage passes (when the builder has started it). NULL: nothing. */
extern void (*v1_params_hook)(const uint8_t track[18]);
extern void (*v1_segment_hook)(int ch);

int v1_frame_handshake(int op); /* EE49E */
void v1_frame_build(void);      /* EE564 */
void v1_frame_tick(void);       /* EDA11 */
void v1_dsp_irq_service(void);  /* EC1E0 */

/* ---- power-up and loop resets (v1_loop.c) ---- */
#define V1_IDLE 0xA558           /* in loop_idle */
#define V1_INPUT_EMPTY 0xA55A    /* the input stage found no input: wait for more */
#define V1_RUN_TEXTRULES 0xA55C  /* the stages with work to do (set by the stage before) */
#define V1_RUN_LEXICAL 0xA55E
#define V1_RUN_PROSODY 0xA560
#define V1_RUN_PARAMGEN 0xA562
#define V1_TRACKS_FULL 0xA564    /* the parameter tracks have no room: wait for the frame builder */
#define V1_PLAYBACK_STATE 0xA566 /* the playback stage's last result (2 = nothing to do) */
#define V1_TRACKS 0x9742         /* 18 pointers to the parameter tracks (128-byte rings at DS:9862) */
#define V1_TRACK_READ 0x96DE     /* the frame builder's position in the tracks */

extern void (*v1_dsp_write_hook)(unsigned word); /* each word sent to the DSP */

void v1_boot_reset(void);   /* EC000, the RAM part */
void v1_loop_restart(void); /* EC021, the RAM part */
void v1_loop_entry(void);   /* F7083 */
int v1_loop_reset(void);    /* F716E */
void v1_power_up(void);     /* EC000 + F7083 */
/* synthesis_main's loop (F7204), for a caller that plays the board: between frame requests, repeat
 * { v1_loop_check_stop(); if (!v1_loop_pass() && !v1_idle_has_work()) break; } */
int v1_loop_check_stop(void); /* the stop request: reset and reply */
int v1_loop_pass(void);       /* one pass; 0 = the loop would idle */
int v1_idle_has_work(void);   /* F73F0, one spin; 0 = wait for an interrupt */

#endif
