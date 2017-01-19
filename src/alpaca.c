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
__nv uint8_t** data_src_base = &data_src;
__nv uint8_t** data_dest_base = &data_dest;
__nv unsigned* data_size_base = &data_size;
__nv volatile unsigned gv_index=0;
__nv volatile unsigned num_dirty_gv=0;
//__nv uint8_t* dirty_arr;
unsigned rcount=0;
unsigned wcount=0;
unsigned tcount=0;
unsigned max_num_dirty_gv=0;
/* Dummy types for offset calculations */
struct _void_type_t {
    void * x;
};
typedef struct _void_type_t void_type_t;

__nv chain_time_t volatile curtime = 0;

/* To update the context, fill-in the unused one and flip the pointer to it */
__nv context_t context_3 = {0};
__nv context_t context_2 = {
	.task = TASK_REF(_entry_task),
	.time = 0,
	.prev_ctx = &context_3,
};
__nv context_t context_1 = {0};
__nv context_t context_0 = {
    .task = TASK_REF(_entry_task),
    .time = 0,
    .prev_ctx = &context_1,
};

__nv context_t * volatile curctx = &context_0;
//KWMAENG

// for internal instrumentation purposes
__nv volatile unsigned _numBoots = 0;
/**
 * @brief Function to be invoked at the beginning of every task
 */
void task_prologue()
{
    _numBoots++;
#if COUNT > 0
	tcount++;
#endif
#if WTGTIME > 0
	TBCTL |= 0x0020; //start timer
#endif
//    task_t *curtask = curctx->task;
	task_t *curtask = curctx->prev_ctx->task;
	//KWMAENG: Now commit must be done on previous task. not current.
	//name curtask for compliance of previous code. Actually it is PREVIOUS TASK!
	//and since curctx's next is currently pointing on previous ctx, it should work!

	if (curctx->time != curtask->last_execute_time) {
	while (gv_index < num_dirty_gv) {
	    //GBUF here!
		uint8_t* w_data_dest = *(data_dest_base + gv_index);
		uint8_t* w_data_src= *(data_src_base + gv_index);
		//unsigned w_data = data[i];
		//unsigned w_data_size = data_size[gv_index];
		unsigned w_data_size = *(data_size_base + gv_index);
		//memcpy(w_data_dest, &data[gv_index], w_data_size);
		memcpy(w_data_dest, w_data_src, w_data_size);
		LOG("final data: %u\r\n",*((unsigned*)w_data_dest));
            	++gv_index;
        }
	//LOG("TRANS: commit end\r\n");
	num_dirty_gv = 0;
	gv_index = 0;
        curtask->last_execute_time = curctx->time;
    	}
	else {
	num_dirty_gv=0;
	}
#if WTGTIME > 0
	TBCTL &= ~(0x0020); //halt timer
#endif
}

/**
 * @brief Transfer control to the given task
 * @details Finalize the current task and jump to the given task.
 *          This function does not return.
 *
 *  TODO: mark this function as bare (i.e. no prologue) for efficiency
 */
void transition_to(task_t *next_task)
{
//	context_t *prev_ctx;
    // reset stack pointer
    // update current task pointer
    // tick logical time
    // jump to next task

    // NOTE: the order of these does not seem to matter, a reboot
    // at any point in this sequence seems to be harmless.
    //
    // NOTE: It might be a bit cleaner to reset the stack and
    // set the current task pointer in the function *prologue* --
    // would need compiler support for this.
    //
    // NOTE: It is harmless to increment the time even if we fail before
    // transitioning to the next task. The reverse, i.e. failure to increment
    // time while having transitioned to the task, would break the
    // semantics of CHAN_IN (aka. sync), which should get the most recently
    // updated value.
    //
    // NOTE: Storing two pointers (one to next and one to current context)
    // does not seem acceptable, because the two would need to be kept
    // consistent in the face of intermittence. But, could keep one pointer
    // to current context and a pointer to next context inside the context
    // structure. The only reason to do that is if it is more efficient --
    // i.e. avoids XORing the index and getting the actual pointer.

    // TODO: handle overflow of timestamp. Some very raw ideas:
    //          * limit the age of values
    //          * a maintainance task that fixes up stored timestamps
    //          * extra bit to mark timestamps as pre/post overflow

    // TODO: re-use the top-of-stack address used in entry point, instead
    //       of hardcoding the address.
    //
    //       Probably need to write a custom entry point in asm, and
    //       use it instead of the C runtime one.
    	context_t *next_ctx; // this should be in a register for efficiency
                         // (if we really care, write this func in asm)
    	next_ctx = (curctx == &context_0 ? &context_2 : &context_0 );

	next_ctx->task = next_task;
	next_ctx->time = curctx->time + 1;
	next_ctx->prev_ctx->time = curctx->time;
	next_ctx->prev_ctx->task = curctx->task;

	curctx = next_ctx;
	
	task_prologue();
	PRINTF("TRANS: to next task\r\n");
    __asm__ volatile ( // volatile because output operands unused by C
        "mov #0x2400, r1\n"
        "br %[ntask]\n"
        :
        : [ntask] "r" (next_task->func)
    );

    // Alternative:
    // task-function prologue:
    //     mov pc, curtask 
    //     mov #0x2400, sp
    //
    // transition_to(next_task->func):
    //     br next_task
}

void write_to_gbuf(uint8_t *data_src, uint8_t *data_dest, size_t var_size) 
//void write_to_gbuf(const void *value, void* data_addr, size_t var_size) 
{
#if WTGTIME > 0
	TBCTL |= 0x0020; //start timer
#endif
	PRINTF("WRITE TO GBUF!! %u\r\n", var_size);
	LOG("WRITE: address of curPointer: %u\r\n", data_dest);
	LOG("num_dirty_gv: %u\r\n", num_dirty_gv);

	*(data_size_base + num_dirty_gv) = var_size;
	*(data_dest_base + num_dirty_gv) = data_dest;
	*(data_src_base + num_dirty_gv) = data_src;
	num_dirty_gv++;
#if WTGTIME > 0
	TBCTL &= ~(0x0020); //halt timer
#endif
}

/** @brief Entry point upon reboot */
int main() {
    _init();


    // Resume execution at the last task that started but did not finish

    // TODO: using the raw transtion would be possible once the
    //       prologue discussed in chain.h is implemented (requires compiler
    //       support)
    // transition_to(curtask);

    task_prologue();

    __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
    );

    return 0; // TODO: write our own entry point and get rid of this
}
