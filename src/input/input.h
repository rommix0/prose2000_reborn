/*
 * The input stage (REFERENCE §15.5): moves host text from the receive ring into the node list.
 *
 * The serial interrupt passes each received character to host_rx_char, which queues it in a 16-byte ring and runs
 * host_escape_parser over it: plain text goes on into a 256-byte ring at DS:EC04, escape sequences either act at once
 * or go into that ring in a compact parsed form (in_escape.c). Each call of the stage takes up to one word from the
 * ring: every character becomes a kind 1 (input text) node, every in-band command a command node. With A-flag 6 the
 * brackets [ ] switch phoneme input on and off.
 *
 * Like the other stages, this code works on the data-segment image of pg.h.
 */
#ifndef INPUT_H
#define INPUT_H

#include "frame_build.h"
#include "pg.h"

/* ---- the receive ring ---- */
#define INPUT_RING 0xEC04    /* 256 bytes */
#define INPUT_READ 0xED04    /* read index */
#define INPUT_WRITE 0xED06   /* write index (the interrupt) */
#define INPUT_AHEAD 0xED08   /* the character read ahead, or -1 */
#define INPUT_XOFF 0xED0A    /* XOFF was sent: send XON once the ring has room */
#define DEMO_PTR 0xEBEE      /* self-test: position in the demo text */
#define DEMO_TEXT_PTR 0x626A /* ROM: the demo text */
#define INPUT_AFLAGS 0xEBEA  /* the last A command passed on (value | second value << 8) */
#define INPUT_NFLAGS 0xEBEC  /* the last N command passed on */

/* ---- the serial receive side and the escape parser (in_escape.c) ---- */
#define RX_RING 0xEBF0      /* 16 bytes, as received */
#define RX_READ 0xEC00      /* read index (the parser) */
#define RX_WRITE 0xEC02     /* write index (host_rx_char) */
#define RX_BUSY 0xDB94      /* the parser is draining RX_RING (interrupts are on meanwhile) */
#define ESC_STATE 0xDB96    /* 1 text, 2 after ESC, 3 in ESC [ */
#define ESC_INDEX 0xDB98    /* byte: the parameter being read */
#define ESC_OVERFLOW 0xDB9A /* a value was over 255 */
#define ESC_PARAMS 0xDB9C   /* 16 words, -1 = not given */
#define SPEED_CAP 0x03BA    /* ROM: 10 words, the highest speed for each fast-read level */
#define IDENTITY 0x03CE     /* ROM: byte, the ESC[E answer (34) */
#define LOW_DEFAULT 0x61FE  /* ROM: 22 bytes, default values of the l parameters */
#define LOW_MAX 0x60E0      /* ROM: 22 words, their maxima */
#define LOW_VALUES 0xEE46   /* 22 bytes: the l parameters */
#define SET_INPUT 0xEE32    /* the host settings (I P v r p a f, N and A flags, V) */
#define SET_PROSODY 0xEE34
#define SET_SPEED 0xEE36
#define SET_RATE 0xEE38
#define SET_PITCH 0xEE3A
#define SET_AMPLITUDE 0xEE3C
#define SET_FAST 0xEE3E
#define HOST_AFLAGS 0xEE42
#define SET_VOICE 0xEE44
#define K_MODE 0xEE6C           /* K1: text and in-band commands are dropped */
#define STOP_REQUEST 0xEE6E     /* S (the synthesis loop acts on it) */
#define END_REQUEST 0xEE70      /* x */
#define QUIT_REQUEST 0xEE76     /* q (the synthesis loop acts on it) */
#define RX_PARITY_ERRORS 0xEE82 /* dword counters of the 8251 error bits */
#define RX_OVERRUN_ERRORS 0xEE86
#define RX_FRAMING_ERRORS 0xEE8A
#define RX_BREAKS 0xEE8E
#define RX_FAST_START 0xDC90   /* = prosody.h FAST_START */
#define RX_FIRST_PHRASE 0xDC92 /* = prosody.h FIRST_PHRASE */

/* ---- the synthesis loop (in_reset.c) ---- */
#define INPUT_IDLE 0xEE5E     /* the input stage runs while this is 0 */
#define PLAYBACK_STATE 0xEE6A /* the last stage_playback_run result; 2 = nothing to do until the generator runs */
#define SELF_TEST 0xEE78      /* byte: the power-up self-test result, reported as ESC[nR */
#define LIST_TAIL 0xC2B2      /* the node list's tail node (LIST_HEAD: its head) */
#define NODE_BASE 0xC2E0      /* the node pool: NODE_COUNT nodes of 10 bytes */
#define NODE_COUNT 0x26A

/* ---- power-up (in_boot.c) ---- */
#define RAM_TESTED 0x2C00     /* the RAM test and the 0x55 fill cover linear 0-2BFF */
#define BOOT_STATUS 0xC200    /* 0, 0x10 after a RAM error, or the failing ROM lane */
#define IO_BASE 0xDB90        /* DS offset of the I/O page 0300:0000 (EF00) */
#define LATCH_PORT 0xF301     /* the peripheral latch port (linear 3401), written directly during boot */
#define BOOT_CS 0xDB74        /* the code segment of the interrupt handlers (D5D6) */
#define INT8_STUB 0x03AA      /* ROM: 15 bytes copied to 0000:0024 for INT 8 (ESC[nL) */
#define DIP_PTR 0xDB86        /* points to the DIP switch port */
#define UART_DATA_PTR 0xDB80  /* points to the 8251 data port */
#define DSP_DATA_PTR 0xDB7A   /* points to the DSP data port */
#define DSP_STATUS_PTR 0xDB78 /* points to the DSP status port */

/* ---- the output ring and the transmitter (host_send) ---- */
#define OUTPUT_RING 0xED0C  /* 256 bytes */
#define OUTPUT_READ 0xEE0C  /* read index (the transmit interrupt) */
#define OUTPUT_WRITE 0xEE0E /* write index */
#define FLOW_PENDING 0xEE10 /* byte: XON (1) / XOFF (2) waiting to be sent (DIP S4-2 on); both cancel out */
#define CR_PENDING 0xEE1A   /* N-flag 15: a CR follows the XON, XOFF or BEL just sent */
#define TX_STOPPED 0xEE12   /* output stopped */
#define TX_RUNNING 0xEE14   /* the transmit interrupt is enabled */
#define TX_PENDING 0xEE16
#define BEL_COUNT 0xEE18 /* byte */
#define HOST_MODE 0xEE40 /* the N flags (bit n-1 = flag n) */

/* ---- the frame interrupt and the phoneme echo (in_dsp.c) ---- */
#define DSP_SPURIOUS 0xDB76   /* interrupts in a row without a frame request; 20 = fatal 0x23 */
#define FRAME_MISSED 0xDB92   /* frame requests in a row with no frame ready */
#define FRAME_READY 0xDBCE    /* a built frame waits in FRAME_BUF */
#define FRAME_BUILDING 0xDBD0 /* dsp_build_frame is filling FRAME_BUF */
#define FRAME_BUF 0xDBD2      /* the 40-word DSP frame */
#define FRAME_SILENT 0xDC22   /* the frame builder's state (frame_builder.silent, .silent_run, .lfsr) */
#define FRAME_SILENT_RUN 0xDC24
#define FRAME_LFSR 0xDC26
#define FRAME_WORK 0xDC28    /* the frame builder's working words (frame_builder.work) */
#define FRAME_PARAMS 0xDC46  /* the 22 parameters of the frame last built (frame_builder.p) */
#define PLAYBACK_WAIT 0xDD80 /* playback reached RING_HOLD: no frames are sent until the generator releases it */
#define ECHO_BUDGET 0xEA56   /* phonemes the echo may still send (segments played) */
#define ECHO_LEAD 0xEA58     /* lead-in phonemes sent, 0-2 */
#define ECHO_READ 0xEA5A     /* the phoneme log: read index, */
#define ECHO_WRITE 0xEA5C    /* write index (paramgen_advance) */
#define ECHO_LOG 0xEA5E      /* and 128 bytes */

/* ---- board ---- */
#define IRQ_NEST 0xEE74     /* nesting count of irq_disable_nested */
#define LATCH 0xDB82        /* copy of the peripheral latch (0x3401) */
#define LATCH_PTR 0xDB84    /* points to the latch */
#define UART_CMD 0xDB7C     /* copy of the 8251 command register */
#define UART_CMD_PTR 0xDB7E /* points to the 8251 control port */
#define PIC_MASK 0xDB88     /* copy of the 8259 interrupt mask */
#define PIC_MASK_PTR 0xDB8A /* points to the 8259 mask port */

/* the DIP switch port as the board returns it (0 = switch on); bit 6 (S4-7) runs the self-test text */
extern unsigned input_dip_port;
/* the 8251 status port as the board returns it; bit 7 = DSR */
extern unsigned host_uart_status;
/* called with each byte the transmitter sends to the host */
extern void (*host_tx_hook)(int c);

/* ^R and ESC[W restart the synthesis loop (code 0x12 / 0x77, see loop_entry) and do not return; ESC[nL executes INT 8
 * (the vector holds no code on the Prose 2000; in the emulator the board ends up restarting). With a hook the parser
 * stops after calling it; without one it carries on (W then acts as x, L as an error, ^R is dropped). */
extern void (*input_restart_hook)(int code);
extern void (*input_int8_hook)(int n);

void host_rx_char(int ch, int status); /* D713F */
void host_escape_parser(void);         /* D658C */
void host_status_report(int n);        /* D6D8C */
int input_put(int c);                  /* DE345 */
void boot_load_rom(const prose_rom *rom);
void power_up(const prose_rom *rom); /* D3100 */
void boot_hardware_init(void);       /* E37F0 code 0x12: D3377, D5E14, D5DF6, D6006, D5EE3 */
int tx_next_byte(void);              /* DE3AE */
int tx_send(void);                   /* D734B */
void output_wait(void);
void node_list_init(void);                                     /* D3900 */
int loop_reset(void);                                          /* E38FF */
void quit_reset(void);                                         /* E3CE7 */
void loop_entry(int code);                                     /* E37F0 */
int loop_check_stop(void);                                     /* E39B5 */
int stage_input_run(void);                                     /* DE09D */
int input_getc(void);                                          /* DE2C5 */
int input_ring_free(void);                                     /* DE2AF */
int node_append(int kind, int ch);                             /* D35C4 */
void irq_disable_nested(void);                                 /* D63E5 */
void irq_enable_nested(void);                                  /* D6576 */
int dip_switches(void);                                        /* D5D68 */
void latch_set(int bits);                                      /* D5D8B */
void latch_clear(int bits);                                    /* D5D74 */
int host_send(int kind, int ch, int count, const int *params); /* DE4C6 */
int output_ring_free(void);                                    /* DE398 */
void uart_tx_start(void);                                      /* D62FE */
void uart_command_set(int bits);                               /* D63C6 */
void uart_command_clear(int bits);                             /* D63AF */
int uart_status(void);                                         /* D63DB */

/* the DSP side (in_dsp.c). dsp_status is the DSP status port as the board returns it: the caller sets USF0 (0x20)
 * when the DSP asks for a frame, and sending the frame clears it. dsp_write_hook gets each frame sent (NULL:
 * accepted); frame_trace_hook each frame built, with its ring position, flag bits and 22 track bytes. */
extern unsigned dsp_status;
extern int (*dsp_write_hook)(const uint16_t *frame, int words);
extern void (*frame_trace_hook)(unsigned pos, int mark, int alt, const uint8_t track[22]);
void dsp_load_rom(const prose_rom *rom);
int dsp_frame_handshake(int op); /* D8949 */
void dsp_build_frame(void);      /* D89CB */
void dsp_frame_tick(void);       /* D643C */
void dsp_irq_service(void);      /* D615F */

#endif
