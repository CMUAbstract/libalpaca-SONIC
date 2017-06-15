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
 * @brief var to iterate over dirtylist
 */
__nv volatile unsigned gv_index=0;
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
	.needCommit = 0,
};
/**
 * @brief current context
 */
__nv context_t * volatile curctx = &context_0;
/**
 * @brief current version which updates at every reboot or transition
 */
__nv volatile unsigned _numBoots = 0;

uint8_t* read_first[MAX_TRACK];
uint8_t* write_first[MAX_TRACK];
__nv uint8_t* war[MAX_TRACK];
__nv unsigned war_size[MAX_TRACK];
unsigned volatile read_first_index = 0;
unsigned volatile write_first_index = 0;
__nv volatile unsigned war_index = 0;

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
	printf("start addr: %x\r\n", start_addr);
	end_addr = _end_addr;
	printf("end addr: %x\r\n", end_addr);
	offset = _start_addr - _start_addr_bak;
	printf("offset: %x\r\n", offset);
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
	if (curctx->needCommit) {
		while (gv_index < war_index) {
			uint8_t* w_data_dest= war[gv_index];
			uint8_t* w_data_src = w_data_dest - offset;
			unsigned w_data_size = war_size[gv_index];
			printf("copy from %x to %x, size %u\r\n", w_data_dest, w_data_src, w_data_size);
			memcpy(w_data_dest, w_data_src, w_data_size);
			++gv_index;
		}
		war_index = 0;
		gv_index = 0;
		read_first_index = 0;
		write_first_index = 0;
		curctx->needCommit = 0;
	}
	else {
		war_index = 0;
		read_first_index = 0;
		write_first_index = 0;
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
	printf("======transition=====\r\n");
	// double-buffered update to deal with power failure
	context_t *next_ctx;
	next_ctx = (curctx == &context_0 ? &context_1 : &context_0 );
	next_ctx->task = next_task;
	next_ctx->needCommit = 1;

	// atomic update of curctx
	curctx = next_ctx;

	// fire task prologue
	task_prologue();
	// jump to next tast
	__asm__ volatile ( // volatile because output operands unused by C
			"mov #0x2400, r1\n"
			"br %[ntask]\n"
			:
			: [ntask] "r" (next_task)
			);
}

bool is_read_first(uint8_t* addr) {
	for (unsigned i = 0; i < read_first_index; ++i) {
		if (read_first[i] == addr)
			return true;
	}
	return false;
}
bool is_write_first(uint8_t* addr) {
	for (unsigned i = 0; i < write_first_index; ++i) {
		if (write_first[i] == addr)
			return true;
	}
	return false;
}
bool is_war(uint8_t* addr) {
	for (unsigned i = 0; i < war_index; ++i) {
		if (war[i] == addr)
			return true;
	}
	return false;
}
void append_read_first(uint8_t* addr) {
	read_first[read_first_index++] = addr;
}
void append_write_first(uint8_t* addr) {
	write_first[write_first_index++] = addr;
}
void append_war(uint8_t* addr, size_t size) {
	war_size[war_index] = size;
	war[war_index++] = addr;
}

/**
 * @brief Called on every read to possible _global_ 
 * @details 1. check if it is reading _global_
 * 			2. if it was never read or written, mark it read-first
 * 			3. if it is WAR, redirect read to buffer
 *			return 0 if it does not need redirection. 1 if it needs redirection.
 */
// slow search, no reset version
uint8_t* check_before_read(uint8_t *addr) {
	if (addr < start_addr || addr > end_addr) 
		return addr;
	printf("read address: %x\r\n", addr);
	if (is_write_first(addr)) {
		printf("write_first! ignore\r\n");
		return addr;
	}
	if (is_war(addr)) {
		printf("is war!!\r\n");
		return addr - offset;
	}
	if (is_read_first(addr)) {
		printf("is read_first!!\r\n");
		return addr;
	}
	printf("mark as read first!!\r\n");
	append_read_first(addr);
	return addr;
}
#if 0
// fast search, slow reset version
bool check_before_read(uint8_t *addr) {
	if (addr < START_ADDR || addr > END_ADDR) 
		return false;
	unsigned index = (unsigned)addr - START_ADDR; //START_ADDR: start address of _global_ vars
	uint8_t status = rw_table[index];
	if (status == IDLE) {
		rw_table[index] = READ_FIRST;
	}
	else if (status == WAR) {
		// redirect read to double buffer!!!
		return true;
	}
	return false;
}
#endif

/**
 * @brief Called on every write to possible _global_ 
 * @details 1. check if it is writinging _global_
 * 			2. if it was never read or written, mark it WRITE_FIRST
 * 			3. if it is READ_FIRST, mark it WAR, and update dirty list & redirect write
 * 			4. if it is WAR, redirect write to buffer
 *			return 0 if it does not need redirection. 1 if it needs redirection.
 */

// slow search, no reset version
uint8_t* check_before_write(uint8_t *addr, size_t size) {
	if (addr < start_addr || addr > end_addr) 
		return addr;
	printf("write address: %x\r\n", addr);
	if (is_write_first(addr)) {
		printf("is write_first!\r\n");
		return addr;
	}
	if (is_war(addr)) {
		printf("is war!\r\n");
		return addr - offset;
	}
	if (is_read_first(addr)) {
		printf("mark as war!\r\n");
		append_war(addr, size);
		return addr - offset;
	}
	printf("mark as write first!\r\n");
	append_write_first(addr);
	return addr;
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

	// jump to curctx
	__asm__ volatile ( // volatile because output operands unused by C
			"br %[nt]\n"
			: /* no outputs */
			: [nt] "r" (curctx->task)
			);

	return 0; 
}
