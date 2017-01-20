#ifndef CHAIN_H
#define CHAIN_H

#include <stddef.h>
#include <stdint.h>

#include <libmsp/mem.h>

#include "repeat.h"

#define TASK_NAME_SIZE 32
#define CHAN_NAME_SIZE 32

#define MAX_DIRTY_SELF_FIELDS 4
#if SBUF == 0
#define MAX_DIRTY_GV_SIZE 33 //temp
#else
#define MAX_DIRTY_GV_SIZE 10 //temp
#endif
#define MAX_DIRTY_ARR_SIZE 4//temp
#define BUFFER_SIZE 33

typedef void (task_func_t)(void);
typedef unsigned chain_time_t;
typedef uint32_t task_mask_t;
typedef uint16_t field_mask_t;
typedef unsigned task_idx_t;

typedef enum {
    CHAN_TYPE_T2T,
    CHAN_TYPE_SELF,
    CHAN_TYPE_ARR,
    CHAN_TYPE_CALL,
    CHAN_TYPE_RETURN,
} chan_type_t;

// TODO: include diag fields only when diagnostics are enabled
typedef struct _chan_diag_t {
//    char source_name[CHAN_NAME_SIZE];
//    char dest_name[CHAN_NAME_SIZE];
#if COUNT > 0
    char name[CHAN_NAME_SIZE];
#endif
} chan_diag_t;

typedef struct _chan_meta_t {
    chan_type_t type;
    chan_diag_t diag;
} chan_meta_t;

//typedef struct _var_meta_t {
//    chain_time_t timestamp;
//} var_meta_t;

typedef struct _self_field_meta_t {
    // Single word (two bytes) value that contains
    // * bit 0: dirty bit (i.e. swap needed)
    // * bit 1: index of the current var buffer from the double buffer pair
    // * bit 5: index of the next var buffer from the double buffer pair
    // This layout is so that we can swap the bytes to flip between buffers and
    // at the same time (atomically) clear the dirty bit.  The dirty bit must
    // be reset in bit 4 before the next swap.
    unsigned idx_pair;
} self_field_meta_t;

typedef struct {
    task_func_t *func;
    task_mask_t mask;
    task_idx_t idx;

    // Dirty self channel fields are ones to which there had been a
    // chan_out. The out value is "staged" in the alternate buffer of
    // the self-channel double-buffer pair for each field. On transition,
    // the buffer index is flipped for dirty fields.
  //  self_field_meta_t *dirty_self_fields[MAX_DIRTY_SELF_FIELDS];
  //  volatile unsigned num_dirty_self_fields;

    volatile chain_time_t last_execute_time; // to execute prologue only once

    char name[TASK_NAME_SIZE];
} task_t;

#define SELF_CHAN_IDX_BIT_DIRTY_CURRENT  0x0001U
#define SELF_CHAN_IDX_BIT_DIRTY_NEXT     0x0100U
#define SELF_CHAN_IDX_BIT_CURRENT        0x0002U
#define SELF_CHAN_IDX_BIT_NEXT           0x0200U

#define VAR_TYPE(type) \
    struct { \
        type value; \
    } \

#define FIELD_TYPE(type) \
    struct { \
        VAR_TYPE(type) var[1]; \
    }
#define SELF_FIELD_TYPE(type) \
    struct { \
        self_field_meta_t meta; \
        VAR_TYPE(type) var[2]; \
    }
#define AGAIN_FIELD_TYPE(type) \
    struct { \
        VAR_TYPE(type) var[1]; \
        uint8_t dirty; \
    }
#define CH_TYPE(src, dest, type) \
    struct _ch_type_ ## src ## _ ## dest ## _ ## type { \
        chan_meta_t meta; \
        struct type data; \
    }
//KWMAENG
#define GV_INIT(type) \
    struct _gv_  ## type ## _ { \
        chan_meta_t meta; \
        struct type data; \
    }
//KWMAENG

/** @brief Declare a value transmittable over a channel
 *  @param  type    Type of the field value
 *  @param  name    Name of the field, include [] suffix to declare an array
 *  @details Metadata field must be the first, so that these
 *           fields can be upcast to a generic type.
 *  NOTE: To make compiler error messages more useful, instead of making
 *        the struct anonymous, we can name it with '_chan_field_ ## name'.
 *        But, this imposes the restriction that field names should be unique
 *        across unrelated channels. Adding the channel name to each CHAN_FIELD
 *        macro is too verbose, so compromising on error msgs.
 *
 *  TODO: could CHAN_FIELD be a special case of CHAN_FIELD_ARRARY with size = 1?
 */
//#define CHAN_FIELD(type, name)                  FIELD_TYPE(type) name
#define CHAN_FIELD(type, name) \
	struct name { \
		FIELD_TYPE(type) var; \
	}
//#define CHAN_FIELD_ARRAY(type, name, size)      FIELD_TYPE(type) name[size]
#define CHAN_FIELD_ARRAY(type, name, size) \
	struct name { \
		FIELD_TYPE(type) var[size]; \
	}	
#if GBUF > 0
#define SELF_CHAN_FIELD(type, name)             CHAN_FIELD(type, name)
#define SELF_CHAN_FIELD_ARRAY(type, name, size) CHAN_FIELD_ARRAY(type, name, size)
#else
#define SELF_CHAN_FIELD(type, name) \
	struct name { \
		SELF_FIELD_TYPE(type) var; \
	}
#define SELF_CHAN_FIELD_ARRAY(type, name, size) \
	struct name { \
	 	SELF_FIELD_TYPE(type) var[size]; \
	}	
#endif
#define SELF_CHAN_FIELD_ARRAY2(type, name, size) \
	struct name { \
		uint16_t varSize; \
		uint16_t num_dirty_arr; \
		uint8_t* pointer[BUFFER_SIZE]; \
		type buffer[BUFFER_SIZE]; \
		FIELD_TYPE(type) var[size]; \
	}
//#endif
/** @brief Execution context */
typedef struct _context_t {
    /** @brief Pointer to the most recently started but not finished task */
    task_t *task;

    /** @brief Logical time, ticks at task boundaries */
    chain_time_t time;

    // TODO: move this to top, just feels cleaner
    struct _context_t *prev_ctx;
} context_t;

//KWMAENG: dirty list is kept outsize
extern uint8_t* data_src[];
extern uint8_t* data_dest[];
extern unsigned data_size[];
extern uint8_t** data_src_base;
extern uint8_t** data_dest_base;
extern unsigned* data_size_base;
extern volatile unsigned _numBoots;
extern volatile unsigned num_dirty_gv;
extern unsigned rcount;
extern unsigned wcount;
extern unsigned tcount;
extern unsigned max_num_dirty_gv;

extern context_t * volatile curctx;




/** @brief Internal macro for constructing name of task symbol */
#define TASK_SYM_NAME(func) _task_ ## func

/** @brief Declare a task
 *
 *  @param idx      Global task index, zero-based
 *  @param func     Pointer to task function
 *
 *   TODO: These do not need to be stored in memory, could be resolved
 *         into literal values and encoded directly in the code instructions.
 *         But, it's not obvious how to implement that with macros (would
 *         need "define inside a define"), so for now create symbols.
 *         The compiler should actually optimize these away.
 *
 *   TODO: Consider creating a table in form of struct and store
 *         for each task: mask, index, name. That way we can
 *         have access to task name for diagnostic output.
 */
#define TASK(idx, func) \
    void func(); \
    __nv task_t TASK_SYM_NAME(func) = { func, (1UL << idx), idx, 0, #func}; \

#define TASK_REF(func) &TASK_SYM_NAME(func)
extern void clear_isDirty();
extern void set_dirty_buf(unsigned* data_base_val, uint8_t** data_dest_base_val, unsigned* data_size_base_val);
/** @brief Function called on every reboot
 *  @details This function usually initializes hardware, such as GPIO
 *           direction. The application must define this function.
 */
extern void init();

//extern void set_dirty_buf(unsigned* db, uint8_t** ddb, unsigned* dsb);
/** @brief First task to run when the application starts
 *  @details Symbol is defined by the ENTRY_TASK macro.
 *           This is not wrapped into a delaration macro, because applications
 *           are not meant to declare tasks -- internal only.
 *
 *  TODO: An alternative would be to have a macro that defines
 *        the curtask symbol and initializes it to the entry task. The
 *        application would be required to have a definition using that macro.
 *        An advantage is that the names of the tasks in the application are
 *        not constrained, and the whole thing is less magical when reading app
 *        code, but slightly more verbose.
 */
extern task_t TASK_SYM_NAME(_entry_task);

/** @brief Declare the first task of the application
 *  @details This macro defines a function with a special name that is
 *           used to initialize the current task pointer.
 *
 *           This does incur the penalty of an extra task transition, but it
 *           happens only once in application lifetime.
 *
 *           The alternatives are to force the user to define functions
 *           with a special name or to define a task pointer symbol outside
 *           of the library.
 */
#define ENTRY_TASK(task) \
    TASK(0, _entry_task) \
    void _entry_task() { TRANSITION_TO(task); }

/** @brief Init function prototype
 *  @details We rely on the special name of this symbol to initialize the
 *           current task pointer. The entry function is defined in the user
 *           application through a macro provided by our header.
 */
void _init();

/** @brief Declare the function to be called on each boot
 *  @details The same notes apply as for entry task.
 */
#define INIT_FUNC(func) void _init() { func(); }

void task_prologue();
void transition_to(task_t *task);
void *chan_in(size_t var_size, uint8_t* chan, size_t field_offset);
void *chan_in_again(size_t var_size, uint8_t* chan, size_t field_offset, ...);
void write_to_gbuf(uint8_t *data_src, uint8_t *data_dest, size_t var_size); 
void chan_out(const void *value,
              size_t var_size, uint8_t* chan, size_t field_offset, ...);
void chan_out_gbuf(const void *value,
              size_t var_size, uint8_t* chan, size_t field_offset, ...);

#define FIELD_COUNT_INNER(type) NUM_FIELDS_ ## type
#define FIELD_COUNT(type) FIELD_COUNT_INNER(type)

/** @brief Initializers for the fields in a channel
 *  @details The user defines a FILED_INIT_<chan_msg_type> macro that,
 *           in braces contains comma separated list of initializers
 *           one for each field, in order of the declaration of the fields.
 *           Each initializer is either
 *             * SELF_FIELD_INITIALIZER, or
 *             * SELF_FIELD_ARRAY_INITIALIZER(count) [only count=2^n supported]
 */
#define SELF_FIELD_META_INITIALIZER { (SELF_CHAN_IDX_BIT_NEXT) }
#define SELF_FIELD_INITIALIZER { SELF_FIELD_META_INITIALIZER }

#define SELF_FIELD_ARRAY_INITIALIZER(count) { REPEAT(count, SELF_FIELD_INITIALIZER) }

#define SELF_FIELDS_INITIALIZER_INNER(type) FIELD_INIT_ ## type
#define SELF_FIELDS_INITIALIZER(type) SELF_FIELDS_INITIALIZER_INNER(type)

//#define CHANNEL(src, dest, type, size) \
//    __nv CH_TYPE(src, dest, type) _ch_ ## src ## _ ## dest __address(size) = \
//        { { CHAN_TYPE_T2T, { #src, #dest } } }

#define CHANNEL(src, dest, type) \
    __nv CH_TYPE(src, dest, type) _ch_ ## src ## _ ## dest = \
        { { CHAN_TYPE_T2T, { #src, #dest } } }

//#define CHANNEL(src, dest, type) \
//    __nv CH_TYPE(src, dest, type) _ch_ ## src ## _ ## dest = \
//        { { CHAN_TYPE_T2T, { #src, #dest } } }

#define SELF_CHANNEL(task, type) \
    __nv CH_TYPE(task, task, type) _ch_ ## task ## _ ## task = \
        { { CHAN_TYPE_SELF, { #task, #task } }, SELF_FIELDS_INITIALIZER(type) }

//KWMAENG: new data type. 
#define GLOBAL_VAR(type, name, ...) GLOBAL_VAR_(type, name, ##__VA_ARGS__, 3, 2)
#define GLOBAL_VAR_(type, name, size, n, ...) GLOBAL_VAR##n(type, name, size)
#define GLOBAL_VAR2(type, name, ...) \
	SELF_CHAN_FIELD(type, name); \
    __nv GV_INIT(name) _gv_ ## name = \
        { { CHAN_TYPE_SELF, { #name } }, {SELF_FIELD_INITIALIZER}}
#if SBUF == 0
#define GLOBAL_VAR3(type, name, size) \
	SELF_CHAN_FIELD_ARRAY(type, name, size); \
    __nv GV_INIT(name) _gv_ ## name = \
        { { CHAN_TYPE_SELF, { #name } }, {SELF_FIELD_ARRAY_INITIALIZER(size)} }
#else
#define GLOBAL_VAR3(type, name, size) \
	SELF_CHAN_FIELD_ARRAY2(type, name, size); \
    __nv GV_INIT(name) _gv_ ## name = \
        { { CHAN_TYPE_ARR, { #name } }, {.varSize=sizeof(type),.num_dirty_arr = 0} }
#endif
#define GLOBAL_SB(type, name, ...) GLOBAL_SB_(type, name, ##__VA_ARGS__, 3, 2)
#define GLOBAL_SB_(type, name, size, n, ...) GLOBAL_SB##n(type, name, size)
//#define GLOBAL_SB2(type, name, ...) \
	CHAN_FIELD(type, name); \
    __nv GV_INIT(name) _gv_ ## name = \
        { { CHAN_TYPE_T2T, { #name } } }
#define GLOBAL_SB2(type, name, ...) __nv type _global_ ## name
#define GLOBAL_SB3(type, name, size) __nv type _global_ ## name[size]
//#define GLOBAL_SB3(type, name, size) \
	CHAN_FIELD_ARRAY(type, name, size); \
    __nv GV_INIT(name) _gv_ ## name = \
        { { CHAN_TYPE_T2T, { #name } } }

//KWMAENG

/** @brief Declare a channel for passing arguments to a callable task
 *  @details Callers would output values into this channels before
 *           transitioning to the callable task.
 *
 *  TODO: should this be associated with the callee task? i.e. the
 *        'callee' argument would be a task name? The concern is
 *        that a callable task might need to be a special type
 *        of a task composed of multiple other tasks (a 'hyper-task').
 * */
#define CALL_CHANNEL(callee, type) \
    __nv CH_TYPE(caller, callee, type) _ch_call_ ## callee = \
        { { CHAN_TYPE_CALL, { #callee, "call:"#callee } } }
#define RET_CHANNEL(callee, type) \
    __nv CH_TYPE(caller, callee, type) _ch_ret_ ## callee = \
        { { CHAN_TYPE_RETURN, { #callee, "ret:"#callee } } }

/** @brief Delcare a channel for receiving results from a callable task
 *  @details Callable tasks output values into this channel, and a
 *           result-processing task would collect the result. The
 *           result-processing task does not need to be dedicated
 *           to this purpose, but the results need to be collected
 *           before the next call to the same task is made.
 */
#define RETURN_CHANNEL(callee, type) \
    __nv CH_TYPE(caller, callee, type) _ch_ret_ ## callee = \
        { { CHAN_TYPE_RETURN, { #callee, "ret:"#callee } } }

/** @brief Declare a multicast channel: one source many destinations
 *  @params name    short name used to refer to the channels from source and destinations
 *  @details Conceptually, the channel is between the specified source and
 *           destinations only. The arbitrary name exists only to simplify referring
 *           to the channel: to avoid having to list all sources and destinations
 *           every time. However, access control is not currently enforced.
 *           Declarations of sources and destinations is necessary for perform
 *           compile-time checks planned for the future.
 */
#define MULTICAST_CHANNEL(type, name, src, dest, ...) \
    __nv CH_TYPE(src, name, type) _ch_mc_ ## src ## _ ## name = \
        { { CHAN_TYPE_MULTICAST, { #src, "mc:" #name } } }

#define CH(src, dest) (&_ch_ ## src ## _ ## dest)
#define SELF_CH(tsk)  CH(tsk, tsk)
//KWMAENG
#define GV_POINTER(type) _gv_ ## type ## _
//#define GV(type)  (&_gv_ ## type)
#define GV(type, ...) GV_(type, ##__VA_ARGS__, 2, 1)
#define GV_(type, i, n, ...) GV##n(type, i)
#define GV1(type, ...) _global_ ## type
#define GV2(type, i) _global_ ## type[i]
//KWMAENG
/* For compatibility */
#define SELF_IN_CH(tsk)  CH(tsk, tsk)
#define SELF_OUT_CH(tsk) CH(tsk, tsk)

/** @brief Reference to a channel used to pass "arguments" to a callable task
 *  @details Each callable task that takes arguments would have one of these.
 * */
#define CALL_CH(callee)  (&_ch_call_ ## callee)

/** @brief Reference to a channel used to receive results from a callable task */
#define RET_CH(callee)  (&_ch_ret_ ## callee)

/** @brief Multicast channel reference
 *  @details Require the source for consistency with other channel types.
 *           In IN, require the one destination that's doing the read for consistency.
 *           In OUT, require the list of destinations for consistency and for
 *           code legibility.
 *           Require name only because of implementation constraints.
 *
 *           NOTE: The name is not pure syntactic sugar, because currently
 *           refering to the multicast channel from the destination by source
 *           alone is not sufficient: there may be more than one channels that
 *           have overlapping destination sets. TODO: disallow this overlap?
 *           The curent implementation resolves this ambiguity using the name.
 *
 *           A separate and more immediate reason for the name is purely
 *           implementation: if if we disallow the overlap ambiguity, this
 *           macro needs to resolve among all the channels from the source
 *           (incl. non-overlapping) -- either we use a name, or we force the
 *           each destination to specify the complete destination list. The
 *           latter is not good since eventually we want the type of the
 *           channel (task-to-task/self/multicast) be transparent to the
 *           application. type nature be transparent , or we use a name.
 */
#define MC_IN_CH(name, src, dest)         (&_ch_mc_ ## src ## _ ## name)
#define MC_OUT_CH(name, src, dest, ...)   (&_ch_mc_ ## src ## _ ## name)

/** @brief Internal macro for counting channel arguments to a variadic macro */
#define NUM_CHANS(...) (sizeof((void *[]){__VA_ARGS__})/sizeof(void *))

/** @brief Read the most recently modified value from one of the given channels
 *  @details This macro retuns a pointer to the most recently modified value
 *           of the requested field.
 *
 *  NOTE: We pass the channel pointer instead of the field pointer
 *        to have access to diagnostic info. The logic in chain_in
 *        only strictly needs the fields, not the channels.
 */
#define READ(chan0, ...) READ_(chan0, ##__VA_ARGS__, 2, 1)
#define READ_(chan0, i, n, ...) READ##n(chan0, i)
#define READ1(chan0, ...) &chan0
#define READ2(chan0, i) &(chan0[i])
//#define READ1(chan0, ...) \
    ((__typeof__((((chan0->data).var).var[0]).value)*)((unsigned char *)chan_in(sizeof((((chan0->data).var).var[0]).value), \
          chan0, offsetof(__typeof__(chan0->data), var))))
//#define READ2(chan0, i) \
    ((__typeof__((((chan0->data).var[0]).var[0]).value)*)((unsigned char *)chan_in(sizeof((((chan0->data).var[0]).var[0]).value), \
          chan0, offsetof(__typeof__(chan0->data), var[i]))))

#define READ_AGAIN(chan0, ...) READ_AGAIN_(chan0, ##__VA_ARGS__, 2, 1)
#define READ_AGAIN_(chan0, i, n, ...) READ_AGAIN##n(chan0, i)
#define READ_AGAIN1(chan0, ...) \
    ((__typeof__((((chan0->data).var).var[0]).value)*)((unsigned char *)chan_in_again(sizeof((((chan0->data).var).var[0]).value), \
          chan0, offsetof(__typeof__(chan0->data), var))))
#define READ_AGAIN2(chan0, i) \
    ((__typeof__((((chan0->data).var[0]).var[0]).value)*)((unsigned char *)chan_in_again(sizeof((((chan0->data).var[0]).var[0]).value), \
          chan0, offsetof(__typeof__(chan0->data), var[i]))))
#define READ_AGAIN_ARR(chan0, i) \ 
    ((__typeof__((((chan0->data).var[0]).var[0]).value)*)((unsigned char *)chan_in_again(sizeof((((chan0->data).var[0]).var[0]).value), \
          chan0, offsetof(__typeof__(chan0->data), var[i]), offsetof(__typeof__(chan0->data), num_dirty_arr),offsetof(__typeof__(chan0->data), buffer),offsetof(__typeof__(chan0->data), pointer))))

//    ((__typeof__((((chan0->data).var[0]).var[0]).value)*)((unsigned char *)chan_in_again(sizeof((((chan0->data).var[0]).var[0]).value), \
//          chan0, offsetof(__typeof__(chan0->data), var[i]), offsetof(__typeof__(chan0->data), num_dirty_arr),offsetof(__typeof__(chan0->data), buffer),offsetof(__typeof__(chan0->data), pointer))))
#define WRITE(val, chan0, ...) WRITE_(val, chan0, ##__VA_ARGS__, 3, 2)
#define WRITE_(val, chan0, i, n, ...) WRITE##n(val, chan0, i)
#define WRITE_TO_GBUF(val, chan0, ...) WRITE_TO_GBUF_(val, chan0, ##__VA_ARGS__, 3, 2)
#define WRITE_TO_GBUF_(val, chan0, i, n, ...) WRITE_TO_GBUF##n(val, chan0, i)
#define WRITE2(val, chan0, ...) chan0 = val;
#define WRITE3(val, chan0, i) (chan0[i]) = val; 
//#define WRITE3(val, chan0, i) \ 
//	chan_out(&val, sizeof((((chan0->data).var[0]).var[0]).value), \
  //           chan0, offsetof(__typeof__(chan0->data), var[i]))
//#define WRITE2(val, chan0, ...) \
//	chan_out(&val, sizeof((((chan0->data).var).var[0]).value), \
  //           chan0, offsetof(__typeof__(chan0->data), var))
//#define WRITE_TO_GBUF2(val, chan0, ...) \ 
//	chan_out_gbuf(&val, sizeof((((chan0->data).var).var[0]).value), \
//             chan0, offsetof(__typeof__(chan0->data), var))
//#define WRITE_TO_GBUF3(val, chan0, i) \ 
//	chan_out_gbuf(&val, sizeof((((chan0->data).var[0]).var[0]).value), \
 //            chan0, offsetof(__typeof__(chan0->data), var[i]))
#define WRITE_TO_GBUF2(val, chan0, ...) write_to_gbuf(&val, &chan0, sizeof(val))
#define WRITE_TO_GBUF3(val, chan0, i) write_to_gbuf(&val, &(chan0[i]), sizeof(val))
#define WRITE_ARR(val, chan0, i) \ 
	chan_out(&val, sizeof((((chan0->data).var[0]).var[0]).value), \
             chan0, offsetof(__typeof__(chan0->data), var[i]), offsetof(__typeof__(chan0->data), num_dirty_arr),offsetof(__typeof__(chan0->data), buffer),offsetof(__typeof__(chan0->data), pointer))

/** @brief Write a value into a channel
 *  @details Note: the list of arguments here is a list of
 *  channels, not of multicast destinations (tasks). A
 *  multicast channel would show up as one argument here.
 */

/** @brief Transfer control to the given task
 *  @param task     Name of the task function
 *  */
#define TRANSITION_TO(task) transition_to(TASK_REF(task))
#endif // CHAIN_H
