#include <stdarg.h>
#include <string.h>
#include <libio/log.h>
#include <msp430.h>

#include "alpaca.h"

#define CHKPT_ACTIVE 0
#define CHKPT_USELESS 1
#define CHKPT_INACTIVE 2
#define CHKPT_NEEDED 3

#define CHKPT_IMMORTAL 0x7F // positive largest

#define RECOVERY_MODE 0
#define NORMAL_MODE 1

#define MAX_TRACK 3000 // temp
#define PACK_BYTE 4

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
/**
 * @brief double buffered context
 */
//extern void task_0();
/**
 * @brief current context
 */
/**
 * @brief current version which updates at every reboot or transition
 */
__nv volatile unsigned _numBoots = 0;

__nv uint8_t* backup[MAX_TRACK];
// TODO: This can be uint8_t
//__nv unsigned backup_size[MAX_TRACK];
#if 1 // temp for debugging
//__nv unsigned backup_bitmask[BITMASK_SIZE]={0};
__nv uint8_t bitmask_counter = 1;
__nv uint8_t need_bitmask_clear = 0;
#endif

__nv uint8_t* start_addr;
__nv uint8_t* end_addr;
__nv unsigned offset;
uint8_t program_end = 0;
__nv volatile isSafeKill = 1;

__nv volatile unsigned regs_0[16];
__nv volatile unsigned regs_1[16];

// size: temp
// temp
__nv uint32_t chkpt_cutvar[55] = {0x66666666};
__nv vars var_record[VAR_NUM] = {{.cutted_num = 0x5555, 
																	.nopable_address = 0x5555}};

// temp size
__nv uint8_t* nvstack[100];

// temp size
__nv unsigned special_stack[40];
uint8_t* special_sp = ((uint8_t*)(&special_stack[0])) - 2;
__nv uint8_t* stack_tracer = ((uint8_t*)(&special_stack[0])) - 2;

__nv context_t context_0 = {
	.cur_reg = NULL,
	.backup_index = 0,
	.stack_tracer = ((uint8_t*)(&special_stack[0])) - 2,
};
__nv context_t context_1 = {
	.stack_tracer = ((uint8_t*)(&special_stack[0])) - 2,
};
__nv context_t * volatile curctx = &context_0;

__nv uint8_t isNoProgress = 0;
__nv uint8_t mode_status = NORMAL_MODE;
//
// testing
//__nv uint8_t chkpt_status[CHKPT_NUM] = {1, 1, 0, 1, 1,     0, 1, 1, 1, 1,
//										1, 1, 1, 1, 1,     1, 1, 1, 1, 0,
//										0, 1, 1, 1, 1,     1, 1, 1, 1, 1,
//										1, 1, 1, 1, 1,     1, 1, 1, 1, 1,
//										1, 1, 1, 1, 1,     1, 1, 1, 1, 1,
//										1, 1, 1, 1, 1,     1, 1, 1, 1, 1,
//										1, 1, 1, 1, 1}; // 1: skip

__nv unsigned chkpt_iterator = 0;
//__nv unsigned var_iterator = 0;
__nv uint8_t chkpt_patching = 0;
//__nv uint8_t logging_patching = 0;

#if 0 // temp disable!
void patch_checkpoints();
// TODO: surround this with checkpoints
void end_run() {
//	chkpt_patching = 1;
//	patch_checkpoints();
}

void patch_checkpoints() {
	for (; chkpt_iterator < CHKPT_NUM; ++chkpt_iterator) {
		// TODO: if CHKPT_NUM gets correctly set, you do not need this if
		if (chkpt_list[chkpt_iterator].fix_to != 0) {
			if (chkpt_status[chkpt_iterator] == CHKPT_USELESS) {
				// remove checkpoint
				chkpt_list[chkpt_iterator].backup = 
					*(chkpt_list[chkpt_iterator].fix_point);
				*(chkpt_list[chkpt_iterator].fix_point) = 
					chkpt_list[chkpt_iterator].fix_to;

//				var_iterator = 1;	

				chkpt_status[chkpt_iterator] = CHKPT_INACTIVE;
			}
			else if (chkpt_status[chkpt_iterator] == CHKPT_NEEDED) {
				// TODO: deal with restoring logging
				*(chkpt_list[chkpt_iterator].fix_point) = 
					chkpt_list[chkpt_iterator].backup;
				chkpt_status[chkpt_iterator] = CHKPT_ACTIVE;
			}
		}
//		if (var_iterator) {
//			uint32_t chkpt_bv = chkpt_cutvar[chkpt_iterator];
//			// patch var_records when removing chkpts
//			// Note: var_iterator starts with 1
//			if (chkpt_bv) {
//				for (; var_iterator <= VAR_NUM; ++var_iterator) {
//					// TODO: This can be faster by reversing the bits
//					if (chkpt_bv & ( (uint32_t)1 << (VAR_NUM - var_iterator) ) ) {
//						// this chkpt was related to var_iterator's var
//						var_record[var_iterator-1].cutted_num--;
//						// TODO: what if this happen multiple times?
//					}
//				}
//			}
//			var_iterator = 0;
//		}
	}
	chkpt_iterator = 0;
//	logging_patching = 1;
	chkpt_patching = 0;
//	patch_logging();
}
#endif

//void patch_logging() {
//	for (; var_iterator < VAR_NUM; ++var_iterator) {
//		if (!var_record[var_iterator].cutted_num) {
//			*(var_record[var_iterator].nopable_address) = (uint32_t)NOP;
//		}
//	}
//
//	logging_patching = 0;
//}



/**
 * @brief Function to be called once to set the global range
 * ideally, it is enough to be called only once, however, currently it is called at the beginning of task_0
 * it can be optimized. (i.e., it needs not be set at runtime)
 */
void set_global_range(uint8_t* _start_addr, uint8_t* _end_addr, uint8_t* _start_addr_bak) {
	start_addr = _start_addr;
	end_addr = _end_addr;
	offset = _start_addr - _start_addr_bak;
	// sanity check
	// TODO: offset calculation can be removed
#if 0 // TEMP disable
	while(offset != global_size) {
		PRINTF("global size calculation is wrong: %u vs %u\r\n", offset, global_size);
	}
#endif
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
__nv unsigned chkpt_count = 0;
void update_checkpoints_pair() {
	chkpt_count = 0;
	for (unsigned i = 0; i < CHKPT_NUM; ++i) {
		if (chkpt_status[i] == CHKPT_ACTIVE) {
			if (chkpt_book[i] < CHKPT_IMMORTAL) {
				if (chkpt_book[i] <= 0)
					chkpt_status[i] = CHKPT_USELESS;
				else
					chkpt_count++;
				chkpt_book[i] = 0;
			}
			else {
				chkpt_count++;
			}
		}
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
	//PRINTF("clear\r\n");
	// TODO: what if it is too large for one E-cycle?
	memset(backup_bitmask, 0, ((unsigned)((global_size+(PACK_BYTE -1))/PACK_BYTE))*sizeof(bitmask_counter));
}
#endif

/**
 * @brief Function resotring on power failure
 */
void restore() {
#if 1 // temp for debugging
	bitmask_counter++;
	if (!bitmask_counter) {
		need_bitmask_clear = 1;
		bitmask_counter++;
	}
	if (need_bitmask_clear) {
		clear_bitmask();
		need_bitmask_clear = 0;
	}
#endif
	// finish patching checkpoint if it was doing it
	//	if (chkpt_patching) {
	//		patch_checkpoints();
	//	}
	//	if (logging_patching) {
	//		patch_logging();
	//	}
	// restore NV globals
	while (curctx->backup_index != 0) {
		uint8_t* w_data_dest = backup[curctx->backup_index - 1];
		uint8_t* w_data_src = w_data_dest - offset;
		//unsigned w_data_size = backup_size[curctx->backup_index - 1];
		memcpy(w_data_dest, w_data_src, PACK_BYTE);
		--(curctx->backup_index);
	}

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
	 * Then 14 is added to SP (for local use)
	 * R12 gets stored in Stack
	 * SP gets saved to R4 */
	// TODO: Nubers will change!
	// Check correctness!!
	__asm__ volatile ("PUSH R12"); // we will use R12 for saving cur_reg
	__asm__ volatile ("MOV %0, R12" :"=m"(curctx->cur_reg)); 

	// currently, R4 holds SP, and PC is at 
	__asm__ volatile ("MOV 26(R1), 0(R12)"); // LR is going to be the next PC

	__asm__ volatile ("MOV R1, 2(R12)"); // We need to add 6 to get the prev SP 
	__asm__ volatile ("ADD #28, 2(R12)");
	// TODO: do we need to save R2 (SR)? Because it is chaned while we
	// subtract from SP anyway (guess it does not matters)
	__asm__ volatile ("MOV R2, 4(R12)");
	__asm__ volatile ("MOV 24(R1), 6(R12)"); // R4
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

	// copy the special stack
	//unsigned stack_size = special_sp  + 2 - (uint8_t*)special_stack;
	////PRINTF("stack size: %u\r\n", stack_size);
	//if (stack_size)
	//	memcpy(curctx->special_stack, special_stack, stack_size);
	uint8_t* last_mod_stack = curctx->stack_tracer > stack_tracer ?
													stack_tracer : curctx->stack_tracer;
	unsigned stack_size = special_sp - last_mod_stack;
	unsigned st_offset = last_mod_stack + 2 - (uint8_t*)special_stack;
	//PRINTF("stack size: %u\r\n", stack_size);
	if (stack_size)
		memcpy(((uint8_t*)curctx->special_stack) + st_offset, 
				((uint8_t*)special_stack) + st_offset, stack_size);

	// copy the sp as well
	curctx->special_sp = special_sp;
//	curctx->stack_tracer = stack_tracer;

	context_t *next_ctx;
	next_ctx = (curctx == &context_0 ? &context_1 : &context_0 );
	next_ctx->cur_reg = curctx->cur_reg == regs_0 ? regs_1 : regs_0;
	next_ctx->backup_index = 0;
	// TEMP disable. is it always correct without this???
	next_ctx->stack_tracer = stack_tracer;

	bitmask_counter++;
	if (!bitmask_counter) {
		need_bitmask_clear = 1;
		bitmask_counter++;
	}
	if (need_bitmask_clear) {
		clear_bitmask();
		need_bitmask_clear = 0;
	}

	// atomic update of curctx
	isNoProgress = 0;
	curctx = next_ctx;
	stack_tracer = special_sp;

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
	context_t* prev_ctx;
	unsigned pc;
	if (curctx->cur_reg == NULL) {
		curctx->cur_reg = regs_0;
		return;
	}
	// TODO: potential bug point
	//else if (curctx->cur_reg == regs_0) {
	else if (curctx == &context_0) {
		//prev_reg = regs_1;
		prev_ctx = &context_1;
	}
	else {
		prev_ctx = &context_0;
		//prev_reg = regs_0;
	}
	if (mode_status == RECOVERY_MODE) {
		//if (isNoProgress) {
		//	// this mean even if it was in the recovery mode,
		//	// it couldn't checkpoint once. weird!!!
		//}
		chkpt_status[prev_ctx->cur_reg[15]] = CHKPT_ACTIVE;	
		chkpt_book[prev_ctx->cur_reg[15]] = CHKPT_IMMORTAL;

		mode_status = NORMAL_MODE;
	}

	if (isNoProgress) {
		// it is stuck
		// restore last passed chkpt
		// TODO: This does not work!!!
		mode_status = RECOVERY_MODE;
	}
	else {
		if (chkpt_book[prev_ctx->cur_reg[15]] < CHKPT_IMMORTAL) {
			chkpt_book[prev_ctx->cur_reg[15]] += 2;
		}
		if (chkpt_book[curctx->cur_reg[15]] < CHKPT_IMMORTAL) {
			chkpt_book[curctx->cur_reg[15]]--;
		}
	}
	isNoProgress = 1;

	// copy the sp as well
	special_sp = prev_ctx->special_sp;
	// copy the special stack
	//unsigned stack_size = special_sp + 2 - (uint8_t*)special_stack;
	unsigned stack_size = special_sp - stack_tracer;
	unsigned st_offset = stack_tracer  + 2 - (uint8_t*)special_stack;
	if (stack_size)
		memcpy(((uint8_t*)special_stack) + st_offset, 
				((uint8_t*)prev_ctx->special_stack) + st_offset, stack_size);
//	unsigned stack_size = special_sp + 2 - (uint8_t*)special_stack;
//	if (stack_size)
//		memcpy(special_stack, 
//				prev_ctx->special_stack, stack_size);

#if 0 //case 2.
	//chkpt_book[prev_reg[15]] = 0;
#endif
#if 0 // case 1.
	//	chkpt_book[prev_reg[15]]++;
#endif
	__asm__ volatile ("MOV %0, R12" :"=m"(prev_ctx->cur_reg)); 
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
	unsigned index = (unsigned)(addr - start_addr);
	return backup_bitmask[(unsigned)(index/PACK_BYTE)] == bitmask_counter;
}

// append war_list and backup
void back_up(uint8_t* addr) {
	// TODO: TMP. We can optimize this
//	if (size < PACK_BYTE) size = PACK_BYTE;
//	if (size > PACK_BYTE) {
//		PRINTF("SIZE is %\r\n", size);
//		while(1);
//	}

	//backup the pack
	uint8_t* addr_aligned = (uint8_t*)((unsigned)addr & ~(PACK_BYTE - 1));
	uint8_t* addr_bak = addr_aligned - offset;
	memcpy(addr_bak, addr_aligned, PACK_BYTE);
	//append dirtylist
	//backup_size[curctx->backup_index] = size;
	//backup[curctx->backup_index] = addr;
	backup[curctx->backup_index] = addr_aligned;
	curctx->backup_index++;

	unsigned index = (unsigned)(addr - start_addr);
	backup_bitmask[(unsigned)(index/PACK_BYTE)] = bitmask_counter;
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
void check_before_write(uint8_t *addr) {
	if (addr < start_addr || addr > end_addr) 
		return;
	if (is_backed_up(addr)) {
		return;
	}
	back_up(addr);
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

