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

#include "chain.h"
__nv self_field_meta_t *dirty_gv[MAX_DIRTY_GV_SIZE];
__nv unsigned data[MAX_DIRTY_GV_SIZE];
__nv unsigned data_size[MAX_DIRTY_GV_SIZE];
__nv uint8_t* data_dest[MAX_DIRTY_GV_SIZE];
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
__nv context_t context_1 = {0};
__nv context_t context_0 = {
    .task = TASK_REF(_entry_task),
    .time = 0,
    .next_ctx = &context_1,
};

__nv context_t * volatile curctx = &context_0;
//KWMAENG
__nv volatile unsigned num_dirty_gv=0;

// for internal instrumentation purposes
__nv volatile unsigned _numBoots = 0;

/**
 * @brief Function to be invoked at the beginning of every task
 */
void task_prologue()
{
#if COUNT > 0
	tcount++;
#endif
#if CTIME > 0
	TBCTL |= 0x0020; //start timer
#endif
//    task_t *curtask = curctx->task;
	task_t *curtask = curctx->next_ctx->task;
	//KWMAENG: Now commit must be done on previous task. not current.
	//name curtask for compliance of previous code. Actually it is PREVIOUS TASK!
	//and since curctx's next is currently pointing on previous ctx, it should work!

//	LOG("0\r\n");

    // Swaps of the self-channel buffer happen on transitions, not restarts.
    // We detect transitions by comparing the current time with a timestamp.
	LOG("curtime:%u last exec time:%u\r\n",curctx->time,curtask->last_execute_time);
	if (curctx->time != curtask->last_execute_time) {

        // Minimize FRAM reads
//        self_field_meta_t **dirty_self_fields = curtask->dirty_self_fields;
//        self_field_meta_t **dirty_self_fields = dirty_gv;

        int i;

        // It is safe to repeat the loop for the same element, because the swap
        // operation clears the dirty bit. We only need to be a little bit careful
        // to decrement the count strictly after the swap.
//	LOG("1\r\n");
//	LOG("%u\r\n",(num_dirty_gv));
	while ((i = num_dirty_gv) > 0) {
#if DBUF > 0
            self_field_meta_t *self_field = dirty_gv[--i];
            if (self_field->idx_pair & SELF_CHAN_IDX_BIT_DIRTY_CURRENT) {
                // Atomically: swap AND clear the dirty bit (by "moving" it over to MSB)
                __asm__ volatile (
                    "SWPB %[idx_pair]\n"
                    : [idx_pair]  "=m" (self_field->idx_pair)
                );
            }
#else
		i--;
		unsigned w_data = data[i];
		uint8_t* w_data_dest = data_dest[i];
		unsigned w_data_size = data_size[i];
       		memcpy(w_data_dest, &w_data, w_data_size);
#endif
            num_dirty_gv = i;
        }

        curtask->last_execute_time = curctx->time;
#if CTIME > 0
	TBCTL &= ~(0x0020); //halt timer
#endif
    	}
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
    context_t *next_ctx; // this should be in a register for efficiency
                         // (if we really care, write this func in asm)
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
	LOG("Before\r\n");
//    	LOG("AFTER\r\n");
    next_ctx = curctx->next_ctx;
    next_ctx->task = next_task;
    next_ctx->time = curctx->time + 1;

    next_ctx->next_ctx = curctx;
//    prev_ctx = curctx;
    curctx = next_ctx;
    	LOG("AFTER\r\n");
///////IF DEAD HERE, THIS BECOMES PROBLEM? NO....THINK ABOUT TIME CHECKING.
//    task_prologue();
	task_prologue();
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

/** @brief Sync: return the most recently updated value of a given field
 *  @param field_name   string name of the field, used for diagnostics
 *  @param var_size     size of the 'variable' type (var_meta_t + value type)
 *  @param count        number of channels to sync
 *  @param ...          channel ptr, field offset in corresponding message type
 *  @return Pointer to the value, ready to be cast to final value type pointer
 */
void *chan_in(const char *field_name, size_t var_size, int count, ...)
{
#if COUNT > 0
	rcount++;
#endif
#if RTIME > 0
	TBCTL |= 0x0020; //start timer
#endif
    va_list ap;
    unsigned i;
    unsigned latest_update = 0;
    uint8_t* value;
#ifdef LIBCHAIN_ENABLE_DIAGNOSTICS
    unsigned latest_chan_idx = 0;
    char curidx;
#endif
//    var_meta_t *var;
//    var_meta_t *latest_var = NULL;

    LIBCHAIN_PRINTF("[%u] %s: in: '%s':", curctx->time,
                    curctx->task->name, field_name);

    va_start(ap, count);

    for (i = 0; i < count; ++i) {
        uint8_t *chan = va_arg(ap, uint8_t *);
        size_t field_offset = va_arg(ap, unsigned);

        uint8_t *chan_data = chan + offsetof(CH_TYPE(_sa, _da, _void_type_t), data);
        chan_meta_t *chan_meta = (chan_meta_t *)(chan +
                                    offsetof(CH_TYPE(_sb, _db, _void_type_t), meta));
        uint8_t *field = chan_data + field_offset;
#if DBUF > 0
        switch (chan_meta->type) {
            case CHAN_TYPE_SELF: {
                self_field_meta_t *self_field = (self_field_meta_t *)field;

                unsigned var_offset =
                    (self_field->idx_pair & SELF_CHAN_IDX_BIT_CURRENT) ? var_size : 0;

        	value = (uint8_t *)(field + offsetof(SELF_FIELD_TYPE(void_type_t), var) + var_offset) + offsetof(VAR_TYPE(void_type_t), value);
                break;
            }
            default:
        	value = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value);
                break;
        }
#else
        	value = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value);
#endif
    }
    va_end(ap);

#if RTIME > 0
	TBCTL &= ~(0x0020); //halt timer
#endif
    return (void *)value;
}

/** @brief Write a value to a field in a channel
 *  @param field_name    string name of the field, used for diagnostics
 *  @param value         pointer to value data
 *  @param var_size      size of the 'variable' type (var_meta_t + value type)
 *  @param count         number of output channels
 *  @param ...           channel ptr, field offset in corresponding message type
 */
void chan_out(const char *field_name, const void *value,
              size_t var_size, int count, ...)
{
#if COUNT > 0
	wcount++;
#endif
#if WTIME > 0
	TBCTL |= 0x0020; //start timer
#endif
    va_list ap;
    int i;
    void* var_value;
//    var_meta_t *var;
#ifdef LIBCHAIN_ENABLE_DIAGNOSTICS
    char curidx;
#endif

    va_start(ap, count);

    for (i = 0; i < count; ++i) {
        uint8_t *chan = va_arg(ap, uint8_t *);
        size_t field_offset = va_arg(ap, unsigned);

        uint8_t *chan_data = chan + offsetof(CH_TYPE(_sa, _da, _void_type_t), data);
        chan_meta_t *chan_meta = (chan_meta_t *)(chan +
                                    offsetof(CH_TYPE(_sb, _db, _void_type_t), meta));
        uint8_t *field = chan_data + field_offset;

        switch (chan_meta->type) {
            case CHAN_TYPE_SELF: {
#if DBUF > 0
                self_field_meta_t *self_field = (self_field_meta_t *)field;
                task_t *curtask = curctx->task;

                unsigned var_offset =
                    (self_field->idx_pair & SELF_CHAN_IDX_BIT_NEXT) ? var_size : 0;

        	var_value = (uint8_t *)(field + offsetof(SELF_FIELD_TYPE(void_type_t), var) + var_offset) + offsetof(VAR_TYPE(void_type_t), value);
                self_field->idx_pair &= ~(SELF_CHAN_IDX_BIT_DIRTY_NEXT);
                self_field->idx_pair |= SELF_CHAN_IDX_BIT_DIRTY_CURRENT;
                dirty_gv[num_dirty_gv++] = self_field;
#else
		memcpy(&data[num_dirty_gv], value, var_size);
                data_size[num_dirty_gv] = var_size;
        	var_value = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value);
                data_dest[num_dirty_gv] = var_value;
		num_dirty_gv++;
#endif
#if COUNT > 0
		if(num_dirty_gv > max_num_dirty_gv) max_num_dirty_gv = num_dirty_gv;
#endif
                break;
            }
            default:
        	var_value = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value);
#if DBUF == 0
        	memcpy(var_value, value, var_size);
#endif
                break;
        }
#if DBUF > 0
        memcpy(var_value, value, var_size);
#endif
    }

    va_end(ap);
#if WTIME > 0
	TBCTL &= ~(0x0020); //halt timer
#endif
}

/** @brief Entry point upon reboot */
int main() {
    _init();

    _numBoots++;

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
