#include <stdarg.h>
#include <string.h>
#include <libio/log.h>
#include <msp430.h>

#ifndef LIBCHAIN_ENABLE_DIAGNOSTICS
#define LIBCHAIN_PRINTF(...)
#else
#include <stdio.h>
#define LIBCHAIN_PRINTF printf
#endif

#include "alpaca.h"

#define IDLE 0
#define WRITE_FIRST 1
#define READ_FIRST 2
#define WAR 3

#define MAX_TRACK 3000 // temp
#define ROM_START 0x4400
#define ROM_END 0xBB80
#define CALL_INST 0x12B0

/**
 * @brief dirtylist to save src address
 */
//__nv uint8_t** data_src_base = &data_src;
/**
 * @brief dirtylist to save dst address
 */
//__nv uint8_t** data_dest_base = &data_dest;
/**
 * @brief dirtylist to save size
 */
//__nv unsigned* data_size_base = &data_size;

/**
 * @brief len of dirtylist
 */
//__nv volatile unsigned num_dirty_gv=0;

/**
 * @brief double buffered context
 */
__nv context_t context_1 = {0};
/**
 * @brief double buffered context
 */
extern void task_0();
__nv context_t context_0 = {
	.task = &task_0,
	.backup_index = 0,
};
/**
 * @brief current context
 */
__nv context_t * volatile curctx = &context_0;
/**
 * @brief current version which updates at every reboot or transition
 */
__nv volatile unsigned _numBoots = 0;

__nv uint8_t* backup[MAX_TRACK];
__nv unsigned backup_size[MAX_TRACK];

__nv uint8_t* start_addr;
__nv uint8_t* end_addr;
__nv unsigned offset;
uint8_t program_end = 0;
__nv volatile isSafeKill = 1;

__nv unsigned regs_0[16];
__nv unsigned regs_1[16];
__nv unsigned* cur_reg = NULL;



/**
 * @brief Function to be called once to set the global range
 * ideally, it is enough to be called only once, however, currently it is called at the beginning of task_0
 * it can be optimized. (i.e., it needs not be set at runtime)
 */
void set_global_range(uint8_t* _start_addr, uint8_t* _end_addr, uint8_t* _start_addr_bak) {
	start_addr = _start_addr;
	end_addr = _end_addr;
	offset = _start_addr - _start_addr_bak;
}

/**
 * @brief Function resotring on power failure
 */
void restore() {
	// restore NV globals
	while (curctx->backup_index != 0) {
		uint8_t* w_data_dest = backup[curctx->backup_index - 1];
		uint8_t* w_data_src = w_data_dest - offset;
		unsigned w_data_size = backup_size[curctx->backup_index - 1];
		memcpy(w_data_dest, w_data_src, w_data_size);
		--(curctx->backup_index);
	}

	// restore regs (including PC)
	restore_regs();
}

/**
 * @brief checkpoint regs
 */
void checkpoint() {
	/* When you call this function:
	 * LR gets stored in Stack
	 * R4 gets stored in Stack
	 * SP grows 4 by the above reason
	 * SP gets saved to R4 */

	__asm__ volatile ("PUSH R12"); // we will use R12 for saving cur_reg
	__asm__ volatile ("MOV %0, R12" :"=m"(cur_reg)); 

	// currently, R4 holds SP, and PC is at 
	__asm__ volatile ("MOV 4(R1), 0(R12)"); // LR is going to be the next PC

	__asm__ volatile ("MOV R1, 2(R12)"); // We need to add 6 to get the prev SP 
	__asm__ volatile ("ADD #6, 2(R12)");
	__asm__ volatile ("MOV R2, 4(R12)");
//	__asm__ volatile ("MOV R3, 6(R12)"); //TODO: Can we skip R3? 
//    __asm__ volatile ("PUSH 8(R1)");     // R4  will appear at 22(R1) [see note above]
	__asm__ volatile ("MOV 2(R1), 6(R12)"); // R4
	__asm__ volatile ("MOV R5, 8(R12)");
	__asm__ volatile ("MOV R6, 10(R12)");
	__asm__ volatile ("MOV R7, 12(R12)");
	__asm__ volatile ("MOV R8, 14(R12)");
	__asm__ volatile ("MOV R9, 16(R12)");
	__asm__ volatile ("MOV R10, 18(R12)");
	__asm__ volatile ("MOV R11, 20(R12)");
	__asm__ volatile ("MOV 0(R1), 22(R12)"); 
	__asm__ volatile ("MOV R13, 24(R12)");
	__asm__ volatile ("MOV R14, 26(R12)");
	__asm__ volatile ("MOV R15, 28(R12)");

	if (cur_reg == regs_0) {
		cur_reg = regs_1;
	}
	else {
		cur_reg = regs_0;
	}
}

/**
 * @brief restore regs
 */
void restore_regs() {
	unsigned *prev_reg;
	if (cur_reg == NULL) {
		cur_reg = regs_0;
		return;
	}
	else if (cur_reg == regs_0) {
		prev_reg = regs_1;
	}
	else {
		prev_reg = regs_0;
	}

	__asm__ volatile ("MOV %0, R12" :"=m"(prev_reg)); 
	__asm__ volatile ("MOV 28(R12), R15");
	__asm__ volatile ("MOV 26(R12), R14");
	__asm__ volatile ("MOV 24(R12), R13");
	__asm__ volatile ("MOV 22(R12), R12");
	__asm__ volatile ("MOV 20(R12), R11");
	__asm__ volatile ("MOV 18(R12), R10");
	__asm__ volatile ("MOV 16(R12), R9");
	__asm__ volatile ("MOV 14(R12), R8");
	__asm__ volatile ("MOV 12(R12), R7");
	__asm__ volatile ("MOV 10(R12), R6");
	__asm__ volatile ("MOV 8(R12), R5");
	__asm__ volatile ("MOV 6(R12), R4");
	__asm__ volatile ("MOV 4(R12), R2");
	__asm__ volatile ("MOV 2(R12), R1");
	__asm__ volatile ("MOV 0(R12), PC");
}

/**
 * @brief Transfer control to the given task
 * @details Finalize the current task and jump to the given task.
 *          This function does not return.
 *
 */
void transition_to(void (*next_task)())
{
	// double-buffered update to deal with power failure
	context_t *next_ctx;
	next_ctx = (curctx == &context_0 ? &context_1 : &context_0 );
	next_ctx->task = next_task;
	next_ctx->backup_index = 0;

	// atomic update of curctx
	program_end = 0;
	curctx = next_ctx;
#if 0
	isSafeKill = 1;
#endif
	// fire task prologue
	//	task_prologue();
	//	// jump to next tast
	//	__asm__ volatile ( // volatile because output operands unused by C
	//			"mov #0x2400, r1\n"
	//			"br %[ntask]\n"
	//			:
	//			: [ntask] "r" (next_task)
	//			);
}

bool is_backed_up(uint8_t* addr) {
	for (unsigned i = 0; i < curctx->backup_index; ++i) {
		if (backup[i] == addr)
			return true;
	}
	return false;
}

// append war_list and backup
void back_up(uint8_t* addr, size_t size) {
	//backup
	uint8_t* addr_bak = addr - offset;
	memcpy(addr_bak, addr, size);
	//append dirtylist
	backup_size[curctx->backup_index] = size;
	backup[curctx->backup_index++] = addr;
}


/**
 * @brief Called on every write to possible _global_ 
 * @details 1. check if it is writinging _global_
 * 			2. if it was never read or written, mark it WRITE_FIRST
 * 			3. if it is READ_FIRST, mark it WAR, and update dirty list & redirect write
 * 			4. if it is WAR, redirect write to buffer
 *			return 0 if it does not need redirection. 1 if it needs redirection.
 */

// slow search, no reset version
void check_before_write(uint8_t *addr, size_t size) {
	if (addr < start_addr || addr > end_addr) 
		return;
	if (is_backed_up(addr)) {
		return;
	}
	back_up(addr, size);
	return;
}

/** @brief Entry point upon reboot */
int main() {
	init();

	// restore on power failure
	restore();
	while (!program_end) {
		program_end = 1;
		((void (*)(void))(curctx->task))();
	}
	// jump to curctx
	//	__asm__ volatile ( // volatile because output operands unused by C
	//			"br %[nt]\n"
	//			: /* no outputs */
	//			: [nt] "r" (curctx->task)
	//			);

	return 0; 
}

void remove_check() {
	unsigned* p = NULL;
	unsigned* prev_word = NULL;
	unsigned* cbw_address = &check_before_write; //function address of check_before_write()

	PRINTF("remove check start\r\n");
	// iterate in word granularity
	for (p = ROM_START; p < ROM_END; ++p) {
		if (*p == CALL_INST) { // CALL inst
			PRINTF("CALL INST!\r\n");
			prev_word = p;
		}
		else if (prev_word != NULL) { // if prev word was CALL inst
			//check the call address
			if (*p == cbw_address) {
				PRINTF("CHANGE TO NOP!\r\n");
				*p = 0x0343; // nop
				*prev_word = 0x0343; // nop
			}
			prev_word = NULL;
		}
	}
}
