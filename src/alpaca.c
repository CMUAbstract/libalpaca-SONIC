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

#define MAX_TRACK 100 // temp

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
 * @brief Function to be invoked at the beginning of every task
 */
void task_prologue()
{
	// increment version
//	if(_numBoots == 0xFFFF){
//		clear_isDirty();
//		++_numBoots;
//	}
//	++_numBoots;
	// commit if needed
	while (curctx->backup_index != 0) {
		uint8_t* w_data_dest = backup[curctx->backup_index - 1];
		uint8_t* w_data_src = w_data_dest - offset;
		unsigned w_data_size = backup_size[curctx->backup_index - 1];
		memcpy(w_data_dest, w_data_src, w_data_size);
		--(curctx->backup_index);
	}
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
	curctx = next_ctx;

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
#if 0
// fast search, slow reset version
bool check_before_write(uint8_t *addr, size_t var_size) {
	if (addr < START_ADDR || addr > END_ADDR) 
		return false;
	unsigned index = (unsigned)addr - START_ADDR; //START_ADDR: start address of _global_ vars
	uint8_t status = rw_table[index];
	if (status == IDLE) {
		rw_table[index] = WRITE_FIRST;
	}
	else if (status == READ_FIRST) {
		rw_table[index] = WAR;
		append_dirtylist(addr, var_size);
		return true;
	}
	else if (status == WAR) {
		// redirect read to double buffer!!!
		return true;
	}
	return false;
}
#endif

/** @brief Entry point upon reboot */
int main() {
	//_init();
	init();

	// (better alternative: transition_to(curctx->task);

	// check for update
	task_prologue();
	while (1) {
		((func*)(curctx->task))();
	}
	// jump to curctx
//	__asm__ volatile ( // volatile because output operands unused by C
//			"br %[nt]\n"
//			: /* no outputs */
//			: [nt] "r" (curctx->task)
//			);

	return 0; 
}
