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
#if SBUF > 0
__nv uint8_t* dirty_arr[MAX_DIRTY_ARR_SIZE];
__nv volatile unsigned num_arr=0;
#endif
#if GBUF > 0
__nv unsigned data[MAX_DIRTY_GV_SIZE];
__nv uint8_t* data_dest[MAX_DIRTY_GV_SIZE];
__nv unsigned data_size[MAX_DIRTY_GV_SIZE];
__nv volatile unsigned gv_index=0;
#else
#if SBUF > 0
__nv volatile unsigned gv_index=0;
#endif
__nv self_field_meta_t *dirty_gv[MAX_DIRTY_GV_SIZE];
#endif
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
#if GBUF == 0
        int i;
#endif
#if GBUF == 0
	while ((i = num_dirty_gv) > 0) {
            self_field_meta_t *self_field = dirty_gv[--i];
            if (self_field->idx_pair & SELF_CHAN_IDX_BIT_DIRTY_CURRENT) {
                // Atomically: swap AND clear the dirty bit (by "moving" it over to MSB)
                __asm__ volatile (
                    "SWPB %[idx_pair]\n"
                    : [idx_pair]  "=m" (self_field->idx_pair)
                );
            }
            	num_dirty_gv = i;
        }
#else
	while (gv_index < num_dirty_gv) {
	    //GBUF here!
		uint8_t* w_data_dest = data_dest[gv_index];
		if (w_data_dest != 0) { //the entry is valid. if 0, it is not-the-head part of struct. ignore!
			//unsigned w_data = data[i];
			unsigned w_data_size = data_size[gv_index];
			memcpy(w_data_dest, &data[gv_index], w_data_size);
			LOG("final data: %u\r\n",*((unsigned*)w_data_dest));
		}
            	++gv_index;
		
        }
	//LOG("TRANS: commit end\r\n");
	num_dirty_gv = 0;
	gv_index = 0;
#endif
#if SBUF > 0
	while ((i = num_arr) > 0) {
		LOG("num arr: %u\r\n",num_arr);
		uint8_t* chan = dirty_arr[--i];
        	uint8_t *chan_data = chan + offsetof(CH_TYPE(_sa, _da, _void_type_t), data);
		uint16_t varSize = *((uint16_t*)chan_data);
		LOG("size is: %u\r\n",varSize);
		uint16_t* num_dirty_arr = chan_data + sizeof(varSize);
		LOG("num_dirty_arr address is: %u\r\n",num_dirty_arr);
		while(gv_index < *num_dirty_arr) {
			LOG("num_dirty_arr address is: %u\r\n",num_dirty_arr);
			//LOG("num_dirty_arr is: %u\r\n",j);
			uint16_t* pointer = num_dirty_arr + 1;
			LOG("pointer base address is: %u\r\n",pointer);
			//LOG("pointer address is: %u\r\n",pointer+j);
			//LOG("pointer is: %u\r\n",*(pointer+j));
			uint8_t* buffer = pointer + BUFFER_SIZE;
			LOG("buffer base address is: %u\r\n",buffer);
			//LOG("buffer address is: %u\r\n",buffer+j*varSize);
			//LOG("buffer is: %u\r\n",*(buffer+j*varSize));
			
			memcpy(*(pointer+gv_index), buffer+(gv_index)*varSize, varSize);
			++gv_index;
		}
		*num_dirty_arr = 0;
		gv_index = 0;
		num_arr = i;
	}	
#endif
        curtask->last_execute_time = curctx->time;
    	}
	else {
#if SBUF > 0
		unsigned i;
		while ((i = num_arr) > 0) { //clear everything here. (Only necessary for power failure)
			uint8_t* chan = dirty_arr[--i];
			uint8_t *chan_data = chan + offsetof(CH_TYPE(_sa, _da, _void_type_t), data);
			uint16_t varSize = *((uint16_t*)chan_data);
			uint16_t* num_dirty_arr = chan_data + sizeof(varSize);

			*num_dirty_arr = 0;
			num_arr = i;
		}
#else
	num_dirty_gv=0;
#endif
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
	//LOG("TRANS: to next task\r\n");
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
void *chan_in(size_t var_size, uint8_t* chan, size_t field_offset)
{
#if COUNT > 0
	rcount++;
#endif
#if RTIME > 0
	TBCTL |= 0x0020; //start timer
#endif
    unsigned i;
    unsigned latest_update = 0;
    uint8_t* value;
#ifdef LIBCHAIN_ENABLE_DIAGNOSTICS
    unsigned latest_chan_idx = 0;
    char curidx;
#endif

        uint8_t *chan_data = chan + offsetof(CH_TYPE(_sa, _da, _void_type_t), data);
        chan_meta_t *chan_meta = (chan_meta_t *)(chan +
                                    offsetof(CH_TYPE(_sb, _db, _void_type_t), meta));
        uint8_t *field = chan_data + field_offset;
    	LOG("READ chan: %u\r\n", chan);
    	LOG("READ chan data: %u\r\n", chan_data);
    	LOG("READ field offset: %u\r\n", field_offset);
    	LOG("READ field: %u\r\n", field);
#if COUNT > 0
	LOG("read: %s\r\n",(chan_meta->diag).name);
#endif
#if GBUF == 0 
	// for double buffer read
        switch (chan_meta->type) {
            case CHAN_TYPE_SELF: {
                self_field_meta_t *self_field = (self_field_meta_t *)field;
		LOG("self read: %u\r\n",self_field->idx_pair & SELF_CHAN_IDX_BIT_NEXT);

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
	//GBUF don't have double buffer. Just read!!
	value = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value);
	LOG("READ: address of curPointer: %u\r\n", value);
#endif

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
void chan_out(const void *value,
              size_t var_size, uint8_t* chan, size_t field_offset, ...)
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

#if SBUF > 0
    va_start(ap, field_offset);
#endif
        uint8_t *chan_data = chan + offsetof(CH_TYPE(_sa, _da, _void_type_t), data);
        chan_meta_t *chan_meta = (chan_meta_t *)(chan +
                                    offsetof(CH_TYPE(_sb, _db, _void_type_t), meta));
        uint8_t *field = chan_data + field_offset;
#if COUNT > 0
	LOG("write: %s\r\n",(chan_meta->diag).name);
#endif
	LOG("WRITE: %u\r\n", chan_meta->type);

#if GBUF == 0
                self_field_meta_t *self_field = (self_field_meta_t *)field;
		LOG("self write: %u\r\n",self_field->idx_pair & SELF_CHAN_IDX_BIT_NEXT);
                unsigned var_offset =
                    (self_field->idx_pair & SELF_CHAN_IDX_BIT_NEXT) ? var_size : 0;

        	var_value = (uint8_t *)(field + offsetof(SELF_FIELD_TYPE(void_type_t), var) + var_offset) + offsetof(VAR_TYPE(void_type_t), value);
                self_field->idx_pair &= ~(SELF_CHAN_IDX_BIT_DIRTY_NEXT);
                self_field->idx_pair |= SELF_CHAN_IDX_BIT_DIRTY_CURRENT;
                dirty_gv[num_dirty_gv++] = self_field;
        	memcpy(var_value, value, var_size);
#endif
#if COUNT > 0
		if(num_dirty_gv > max_num_dirty_gv) max_num_dirty_gv = num_dirty_gv;
#endif
#if SBUF > 0
        	size_t num_dirty_arr_offset = va_arg(ap, unsigned);
        	size_t buffer_offset = va_arg(ap, unsigned);
        	size_t pointer_offset = va_arg(ap, unsigned);
		unsigned* num_dirty_arr = chan_data + num_dirty_arr_offset;
		uint16_t* pointer = chan_data + pointer_offset;
		uint8_t* buffer = chan_data + buffer_offset;
		uint8_t isAlreadyIn=0;
		for (unsigned i =0; i<num_arr; i++){
			if (dirty_arr[i]==(uint8_t*)chan) isAlreadyIn=1;
			LOG("already in\r\n");
		}
		if(!isAlreadyIn) {
			dirty_arr[num_arr++]=(uint8_t*)chan;
			LOG("not already in\r\n");
		}
		self_field_meta_t *self_field = (self_field_meta_t *)field;

        	*(pointer+(*num_dirty_arr)) = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value); //points to location to write
        	var_value = (uint8_t *)(buffer + var_size*(*num_dirty_arr)); //points to buffer
        	memcpy(var_value, value, var_size);
		
		(*num_dirty_arr)++;
#endif
        	var_value = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value);
        	memcpy(var_value, value, var_size);

#if SBUF > 0
    va_end(ap);
#endif
#if WTIME > 0
	TBCTL &= ~(0x0020); //halt timer
#endif
}
void write_to_gbuf(uint8_t *value, uint8_t *data_addr, size_t var_size) 
//void write_to_gbuf(const void *value, void* data_addr, size_t var_size) 
{
#if WTGTIME > 0
	TBCTL |= 0x0020; //start timer
#endif
	LOG("WRITE TO GBUF!!\r\n");
	LOG("WRITE: address of curPointer: %u\r\n", data_addr);
	memcpy(&data[num_dirty_gv], value, var_size);
	data_size[num_dirty_gv] = var_size;
	data_dest[num_dirty_gv] = data_addr;
	if (var_size > sizeof(data[num_dirty_gv])) { //if data is struct, it may go beyond unsigned. In that case, invalidate succeeding values
		unsigned quotient = (unsigned)((var_size-1)/sizeof(data[num_dirty_gv]))+1;	
		LOG("quotient: %u\r\n",quotient);
		for (unsigned i=1;i<quotient;++i) { //note: starts from 1, not 0
			//data_size[num_dirty_gv+i]=0; //size doesn't matter! trash anyways
			data_dest[num_dirty_gv+i]=0;
		}
		num_dirty_gv += quotient;
	}
	else {
		num_dirty_gv++;
	}
#if WTGTIME > 0
	TBCTL &= ~(0x0020); //halt timer
#endif
}

void chan_out_gbuf(const void *value,
              size_t var_size, uint8_t* chan, size_t field_offset, ...)
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

#if SBUF > 0
    va_start(ap, field_offset);
#endif
    	LOG("WRITE chan: %u\r\n", chan);
    	//LOG("WRITE field: %u\r\n", field);
        uint8_t *chan_data = chan + offsetof(CH_TYPE(_sa, _da, _void_type_t), data);
        chan_meta_t *chan_meta = (chan_meta_t *)(chan +
                                    offsetof(CH_TYPE(_sb, _db, _void_type_t), meta));
        uint8_t *field = chan_data + field_offset;
    	LOG("WRITE chan data: %u\r\n", chan_data);
    	LOG("WRITE field offset: %u\r\n", field_offset);
    	LOG("WRITE field: %u\r\n", field);
#if COUNT > 0
	LOG("write: %s\r\n",(chan_meta->diag).name);
#endif
#if GBUF == 0
                self_field_meta_t *self_field = (self_field_meta_t *)field;
		LOG("self write: %u\r\n",self_field->idx_pair & SELF_CHAN_IDX_BIT_NEXT);
                unsigned var_offset =
                    (self_field->idx_pair & SELF_CHAN_IDX_BIT_NEXT) ? var_size : 0;

        	var_value = (uint8_t *)(field + offsetof(SELF_FIELD_TYPE(void_type_t), var) + var_offset) + offsetof(VAR_TYPE(void_type_t), value);
                self_field->idx_pair &= ~(SELF_CHAN_IDX_BIT_DIRTY_NEXT);
                self_field->idx_pair |= SELF_CHAN_IDX_BIT_DIRTY_CURRENT;
                dirty_gv[num_dirty_gv++] = self_field;
        	memcpy(var_value, value, var_size);
#else
		//this is for GBUF
		memcpy(&data[num_dirty_gv], value, var_size);
                data_size[num_dirty_gv] = var_size;
        	var_value = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value);
		data_dest[num_dirty_gv] = var_value;
		LOG("WRITE: address of curPointer: %u\r\n", var_value);
		if (var_size > sizeof(data[num_dirty_gv])) { //if data is struct, it may go beyond unsigned. In that case, invalidate succeeding values
			LOG("var_size %u is larger than %u\r\n",var_size,sizeof(data[num_dirty_gv]));
			unsigned quotient = (unsigned)((var_size-1)/sizeof(data[num_dirty_gv]))+1;	
			LOG("quotient: %u\r\n",quotient);
			for (unsigned i=1;i<quotient;++i) { //note: starts from 1, not 0
				//data_size[num_dirty_gv+i]=0; //size doesn't matter! trash anyways
				data_dest[num_dirty_gv+i]=0;
			}
			num_dirty_gv += quotient;
		}
		else {
			num_dirty_gv++;
		}
#endif
#if COUNT > 0
		if(num_dirty_gv > max_num_dirty_gv) max_num_dirty_gv = num_dirty_gv;
#endif
#if SBUF > 0
        	size_t num_dirty_arr_offset = va_arg(ap, unsigned);
        	size_t buffer_offset = va_arg(ap, unsigned);
        	size_t pointer_offset = va_arg(ap, unsigned);
		unsigned* num_dirty_arr = chan_data + num_dirty_arr_offset;
		uint16_t* pointer = chan_data + pointer_offset;
		uint8_t* buffer = chan_data + buffer_offset;
		uint8_t isAlreadyIn=0;
		for (unsigned i =0; i<num_arr; i++){
			if (dirty_arr[i]==(uint8_t*)chan) isAlreadyIn=1;
			LOG("already in\r\n");
		}
		if(!isAlreadyIn) {
			dirty_arr[num_arr++]=(uint8_t*)chan;
			LOG("not already in\r\n");
		}
		self_field_meta_t *self_field = (self_field_meta_t *)field;

        	*(pointer+(*num_dirty_arr)) = (uint8_t *)(field + offsetof(FIELD_TYPE(void_type_t), var)) + offsetof(VAR_TYPE(void_type_t), value); //points to location to write
        	var_value = (uint8_t *)(buffer + var_size*(*num_dirty_arr)); //points to buffer
        	memcpy(var_value, value, var_size);
		
		(*num_dirty_arr)++;
#endif

#if SBUF > 0
    va_end(ap);
#endif
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