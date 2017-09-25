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

#define CHKPT_ACTIVE 0
#define CHKPT_USELESS 1
#define CHKPT_INACTIVE 2
#define CHKPT_NEEDED 3

#define MAX_TRACK 3000 // temp
#define ROM_START 0x4400
#define ROM_END 0xBB80
#define CALL_INST 0x12B0
#define BITMASK_SIZE 4000

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
//extern void task_0();
__nv context_t context_0 = {
	.cur_reg = NULL,
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
#if 1 // temp for debugging
__nv unsigned backup_bitmask[BITMASK_SIZE]={0};
__nv unsigned bitmask_counter = 1;
#endif

__nv uint8_t* start_addr;
__nv uint8_t* end_addr;
__nv unsigned offset;
uint8_t program_end = 0;
__nv volatile isSafeKill = 1;

__nv volatile unsigned regs_0[16];
__nv volatile unsigned regs_1[16];

typedef struct _chkpt_info {
	unsigned backup; 
	unsigned* fix_point;
	unsigned fix_to;
} chkpt_info;
// size: temp
__nv chkpt_info chkpt_list[CHKPT_NUM] = {{.backup = 0x7777, .fix_point = 0x7777, .fix_to = 0x7777}};

// testing
//__nv uint8_t chkpt_status[CHKPT_NUM] = {1, 1, 0, 1, 1,     0, 1, 1, 1, 1,
//										1, 1, 1, 1, 1,     1, 1, 1, 1, 0,
//										0, 1, 1, 1, 1,     1, 1, 1, 1, 1,
//										1, 1, 1, 1, 1,     1, 1, 1, 1, 1,
//										1, 1, 1, 1, 1,     1, 1, 1, 1, 1,
//										1, 1, 1, 1, 1,     1, 1, 1, 1, 1,
//										1, 1, 1, 1, 1}; // 1: skip

__nv chkpt_iterator = 0;
__nv chkpt_patching = 0;

void patch_checkpoints();
// TODO: surround this with checkpoints
void end_run() {
	chkpt_patching = 1;
	patch_checkpoints();
}
void patch_checkpoints() {
	for (; chkpt_iterator < CHKPT_NUM; ++chkpt_iterator) {
		// TODO: if CHKPT_NUM gets correctly set, you do not need this if
		if (chkpt_list[chkpt_iterator].fix_to != 0) {
			if (chkpt_status[chkpt_iterator] == CHKPT_USELESS) {
				// remove checkpoint
				chkpt_list[chkpt_iterator].backup = *(chkpt_list[chkpt_iterator].fix_point);
				*(chkpt_list[chkpt_iterator].fix_point) = chkpt_list[chkpt_iterator].fix_to;
				chkpt_status[chkpt_iterator] = CHKPT_INACTIVE;
			}
			else if (chkpt_status[chkpt_iterator] == CHKPT_NEEDED) {
				*(chkpt_list[chkpt_iterator].fix_point) = chkpt_list[chkpt_iterator].backup;
				chkpt_status[chkpt_iterator] = CHKPT_ACTIVE;
			}
		}
	}
	chkpt_iterator = 0;
	chkpt_patching = 0;
}



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

void update_checkpoints_naive() {
	for (unsigned i = 0; i < CHKPT_NUM; ++i) {
		if (!chkpt_book[i])
			chkpt_status[i] = CHKPT_USELESS;
		chkpt_book[i] = 0;
	}
}

void update_checkpoints_hysteresis() {
	for (unsigned i = 0; i < CHKPT_NUM; ++i) {
		if (chkpt_book[i] > 5)
			chkpt_status[i] = CHKPT_USELESS;
	}
}

void update_checkpoints_pair() {
	for (unsigned i = 0; i < CHKPT_NUM; ++i) {
		if (chkpt_book[i] <= 0)
			chkpt_status[i] = CHKPT_USELESS;
		chkpt_book[i] = 0;
	}
}

void update_hysteresis(unsigned last_chkpt) {
	// last checkpoint should never be removed
	chkpt_book[last_chkpt] = 0;
}

void make_table(uint8_t* addr) {
	// test dummy
}

#if 1 // temp for debugging
void clear_bitmask() {
	my_memset(backup_bitmask, 0, BITMASK_SIZE*2);
}
#endif

/**
 * @brief Function resotring on power failure
 */
void restore() {
	// test
#if 1 // temp for debugging
	bitmask_counter++;
	if (!bitmask_counter) {
		bitmask_counter++;
		clear_bitmask();
	}
#endif
	// finish patching checkpoint if it was doing it
	if (chkpt_patching) {
		patch_checkpoints();
	}
	PRINTF("restore!\r\n");
	// restore NV globals
	while (curctx->backup_index != 0) {
		uint8_t* w_data_dest = backup[curctx->backup_index - 1];
		PRINTF("restoring %x\r\n", w_data_dest);
		uint8_t* w_data_src = w_data_dest - offset;
		unsigned w_data_size = backup_size[curctx->backup_index - 1];
		memcpy(w_data_dest, w_data_src, w_data_size);
		--(curctx->backup_index);
	}

	PRINTF("restore global done!\r\n");
	// restore regs (including PC)
	restore_regs();
}


unsigned return_pc() {
	unsigned pc;
	__asm__ volatile ("MOV 2(R1), %0":"=m"(pc));

	return pc;
}
/**
 * @brief checkpoint regs
 */
void checkpoint() {
	unsigned r12;
	/* When you call this function:
	 * LR gets stored in Stack
	 * R4 gets stored in Stack
	 * Then 12 is added to SP (for local use)
	 * SP grows 4 by the above reason
	 * SP gets saved to R4 */

	__asm__ volatile ("PUSH R12"); // we will use R12 for saving cur_reg
	__asm__ volatile ("MOV %0, R12" :"=m"(curctx->cur_reg)); 

	// currently, R4 holds SP, and PC is at 
	__asm__ volatile ("MOV 16(R1), 0(R12)"); // LR is going to be the next PC

	__asm__ volatile ("MOV R1, 2(R12)"); // We need to add 6 to get the prev SP 
	__asm__ volatile ("ADD #18, 2(R12)");
	__asm__ volatile ("MOV R2, 4(R12)");
	__asm__ volatile ("MOV 14(R1), 6(R12)"); // R4
	__asm__ volatile ("MOV R5, 8(R12)");
	__asm__ volatile ("MOV R6, 10(R12)");
	__asm__ volatile ("MOV R7, 12(R12)");
	__asm__ volatile ("MOV R8, 14(R12)");
	__asm__ volatile ("MOV R9, 16(R12)");
	__asm__ volatile ("MOV R10, 18(R12)");
	__asm__ volatile ("MOV R11, 20(R12)");
	// TODO: Do we ever have to save R12, R13, and SR? (Or maybe even R14, R15.) 
	// Maybe if we only place checkpointing at the end of a basicblock,
	// We do not need to save these

	__asm__ volatile ("MOV 0(R1), 22(R12)"); 
	__asm__ volatile ("MOV R13, 24(R12)");
	__asm__ volatile ("MOV R14, 26(R12)");
	__asm__ volatile ("MOV R15, 28(R12)");

	__asm__ volatile ("MOV R12, %0":"=m"(r12));
	context_t *next_ctx;
	next_ctx = (curctx == &context_0 ? &context_1 : &context_0 );
	next_ctx->cur_reg = curctx->cur_reg == regs_0 ? regs_1 : regs_0;
	next_ctx->backup_index = 0;

#if 1 // temp for debugging
	bitmask_counter++;
	if (!bitmask_counter) {
		bitmask_counter++;
		clear_bitmask();
	}
#endif

	// atomic update of curctx
	curctx = next_ctx;

	// TODO: Do not know for sure, doing conservative thing
	// Do we need this?
	__asm__ volatile ("MOV %0, R12":"=m"(r12));
	__asm__ volatile ("MOV 4(R12), R2");
	__asm__ volatile ("MOV 24(R12), R13");
	__asm__ volatile ("MOV 26(R12), R14");
	__asm__ volatile ("MOV 28(R12), R15");


	//PMMCTL0 = PMMPW | PMMSWPOR;
	__asm__ volatile ("POP R12"); // we will use R12 for saving cur_reg
}

void print_book() {
	for (unsigned i = 0; i < 10; ++i) {

	}
}

/**
 * @brief restore regs
 */
void restore_regs() {
	unsigned *prev_reg;
	unsigned pc;
	if (curctx->cur_reg == NULL) {
		curctx->cur_reg = regs_0;
		return;
	}
	// TODO: potential bug point
	else if (curctx->cur_reg == regs_0) {
		prev_reg = regs_1;
	}
	else {
		prev_reg = regs_0;
	}
	chkpt_book[prev_reg[15]] += 2;
	chkpt_book[curctx->cur_reg[15]]--;
#if 0 //case 2.
	//chkpt_book[prev_reg[15]] = 0;
#endif
#if 0 // case 1.
	//	chkpt_book[prev_reg[15]]++;
#endif
	__asm__ volatile ("MOV %0, R12" :"=m"(prev_reg)); 
	// TODO: do we need R15 - R12 / R2?
	__asm__ volatile ("MOV 28(R12), R15");
	__asm__ volatile ("MOV 26(R12), R14");
	__asm__ volatile ("MOV 24(R12), R13");
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
	__asm__ volatile ("MOV 0(R12), %0" :"=m"(pc));
	__asm__ volatile ("MOV 22(R12), R12");
	__asm__ volatile ("MOV %0, R0" :"=m"(pc));
}

/**
 * @brief Transfer control to the given task
 * @details Finalize the current task and jump to the given task.
 *          This function does not return.
 *
 */
//void transition_to(void (*next_task)())
//{
//	// double-buffered update to deal with power failure
//	context_t *next_ctx;
//	next_ctx = (curctx == &context_0 ? &context_1 : &context_0 );
//	next_ctx->task = next_task;
//	next_ctx->backup_index = 0;
//
//	// atomic update of curctx
//	program_end = 0;
//	curctx = next_ctx;
//#if 0
//	isSafeKill = 1;
//#endif
//	// fire task prologue
//	//	task_prologue();
//	//	// jump to next tast
//	//	__asm__ volatile ( // volatile because output operands unused by C
//	//			"mov #0x2400, r1\n"
//	//			"br %[ntask]\n"
//	//			:
//	//			: [ntask] "r" (next_task)
//	//			);
//}

bool is_backed_up(uint8_t* addr) {
#if 1 // temp for debugging
	unsigned index = (unsigned)(addr - start_addr);
	return backup_bitmask[index] == bitmask_counter;
#endif
#if 0 
	for (unsigned i = 0; i < curctx->backup_index; ++i) {
		if (backup[i] == addr)
			return true;
	}
	return false;
#endif
}

// append war_list and backup
void back_up(uint8_t* addr, size_t size) {
	PRINTF("back up: %x\r\n", addr);
	//backup
	uint8_t* addr_bak = addr - offset;
	memcpy(addr_bak, addr, size);
	//append dirtylist
	backup_size[curctx->backup_index] = size;
	backup[curctx->backup_index] = addr;
	curctx->backup_index++;

#if 1 // temp for debugging
	unsigned index = (unsigned)(addr - start_addr);
	backup_bitmask[index] = bitmask_counter;
#endif
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
//int main() {
//	init();
//
//	// restore on power failure
//	restore();
//	while (!program_end) {
//		program_end = 1;
//		((void (*)(void))(curctx->task))();
//	}
//	// jump to curctx
//	//	__asm__ volatile ( // volatile because output operands unused by C
//	//			"br %[nt]\n"
//	//			: /* no outputs */
//	//			: [nt] "r" (curctx->task)
//	//			);
//
//	return 0; 
//}

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
