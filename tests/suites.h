#ifndef _SUITES_H_
#define _SUITES_H_

#include "dctest.h"

extern const dc_suite suite_mov;
extern const dc_suite suite_arith;
extern const dc_suite suite_logic;
extern const dc_suite suite_shift;
extern const dc_suite suite_branch;
extern const dc_suite suite_syscontrol;
extern const dc_suite suite_fpu_simple;
extern const dc_suite suite_fpu_control;
extern const dc_suite suite_fpu_graph;
extern const dc_suite suite_fpu_excepciones;
extern const dc_suite suite_decodificacion;
extern const dc_suite suite_cobertura;

/* Piezas del arranque por BIOS: no son opcodes. */
extern const dc_suite suite_sistema;
extern const dc_suite suite_gdrom;

/* El formato de los parametros del tile accelerator. Tampoco es un opcode. */
extern const dc_suite suite_ta;

/* La ventana de control P4 (docs/mmu-plan.md, fase 1). Tampoco es un opcode. */
extern const dc_suite suite_mmu;

/* Los temporizadores: el watchdog y los tres del TMU. */
extern const dc_suite suite_wdt;
extern const dc_suite suite_tmu;

/* Las dos ventanas de la RAM de video del PVR (vram.h). */
extern const dc_suite suite_vram;

#endif /* _SUITES_H_ */
