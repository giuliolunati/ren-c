//
//  File: %m-stack.c
//  Summary: "data and function call stack implementation"
//  Section: memory
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


#define CHUNKER_FROM_CHUNK(c) \
    cast(struct Reb_Chunker*, \
        cast(REBYTE*, (c)) - (c)->offset \
        - offsetof(struct Reb_Chunker, payload))

//
//  Init_Stacks: C
//
void Init_Stacks(REBCNT size)
{
    // We always keep one chunker around for the first chunk push, and prep
    // one chunk so that the push and drop routines never worry about testing
    // for the empty case.

    TG_Root_Chunker = cast(struct Reb_Chunker*, Alloc_Mem(BASE_CHUNKER_SIZE + CS_CHUNKER_PAYLOAD));
#if !defined(NDEBUG)
    memset(TG_Root_Chunker, 0xBD, sizeof(struct Reb_Chunker));
#endif
    TG_Root_Chunker->next = NULL;
    TG_Root_Chunker->size = CS_CHUNKER_PAYLOAD;
    TG_Top_Chunk = cast(struct Reb_Chunk*, &TG_Root_Chunker->payload);
    TG_Top_Chunk->prev = NULL;
    TG_Top_Chunk->size.bits = BASE_CHUNK_SIZE; // zero values for initial chunk
    TG_Top_Chunk->offset = 0;

    // Implicit termination trick--see VALUE_FLAG_NOT_END and related notes
    cast(
        struct Reb_Chunk*, cast(REBYTE*, TG_Top_Chunk) + BASE_CHUNK_SIZE
    )->size.bits = 0;
    assert(IS_END(&TG_Top_Chunk->values[0]));

    // Start the data stack out with just one element in it, and make it an
    // unwritable trash for the debug build.  This helps avoid both accidental
    // reads and writes of an empty stack, as well as meaning that indices
    // into the data stack can be unsigned (no need for -1 to mean empty,
    // because 0 can)
    {
        DS_Array = Make_Array(1);
        DS_Movable_Base = ARR_HEAD(DS_Array);

        SET_TRASH_SAFE(ARR_HEAD(DS_Array));

    #if !defined(NDEBUG)
        MARK_CELL_UNWRITABLE_IF_DEBUG(ARR_HEAD(DS_Array));
    #endif

        // The END marker will signal DS_PUSH that it has run out of space,
        // and it will perform the allocation at that time.
        //
        SET_ARRAY_LEN(DS_Array, 1);
        SET_END(ARR_TAIL(DS_Array));
        ASSERT_ARRAY(DS_Array);

        // Reuse the expansion logic that happens on a DS_PUSH to get the
        // initial stack size.  It requires you to be on an END to run.  Then
        // drop the hypothetical thing pushed.
        //
        DS_Index = 1;
        Expand_Data_Stack_May_Fail(size);
        DS_DROP;
    }

    // Call stack (includes pending functions, parens...anything that sets
    // up a `struct Reb_Frame` and calls Do_Core())  Singly linked.
    //
    TG_Frame_Stack = NULL;
}


//
//  Shutdown_Stacks: C
//
void Shutdown_Stacks(void)
{
    assert(FS_TOP == NULL);
    assert(DSP == 0); // !!! Why not free data stack here?

    assert(TG_Top_Chunk == cast(struct Reb_Chunk*, &TG_Root_Chunker->payload));

    // Because we always keep one chunker of headroom allocated, and the
    // push/drop is not designed to manage the last chunk, we *might* have
    // that next chunk of headroom still allocated.
    //
    if (TG_Root_Chunker->next)
        Free_Mem(TG_Root_Chunker->next, TG_Root_Chunker->next->size + BASE_CHUNKER_SIZE);

    // OTOH we always have to free the root chunker.
    //
    Free_Mem(TG_Root_Chunker, TG_Root_Chunker->size + BASE_CHUNKER_SIZE);
}


//
//  Expand_Data_Stack_May_Fail: C
//
// The data stack maintains an invariant that you may never push an END to it.
// So each push looks to see if it's pushing to a cell that contains an END
// and if so requests an expansion.
//
// WARNING: This will invalidate any extant pointers to REBVALs living in
// the stack.  It is for this reason that stack access should be done by
// REBDSP "data stack pointers" and not by REBVAL* across *any* operation
// which could do a push or pop.  (Currently stable w.r.t. pop but there may
// be compaction at some point.)
//
void Expand_Data_Stack_May_Fail(REBCNT amount)
{
    REBCNT len_old = ARR_LEN(DS_Array);
    REBCNT len_new;
    REBCNT n;
    REBVAL *value;

    // The current requests for expansion should only happen when the stack
    // is at its end.  Sanity check that.
    //
    assert(IS_END(DS_TOP));
    assert(DS_TOP == ARR_TAIL(DS_Array));
    assert(DS_TOP - ARR_HEAD(DS_Array) == len_old);

    // If adding in the requested amount would overflow the stack limit, then
    // give a data stack overflow error.
    //
    if (SER_REST(ARR_SERIES(DS_Array)) + amount >= STACK_LIMIT)
        Trap_Stack_Overflow();

    Extend_Series(ARR_SERIES(DS_Array), amount);

    // Update the global pointer representing the base of the stack that
    // likely was moved by the above allocation.  (It's not necessarily a
    // huge win to cache it, but it turns data stack access from a double
    // dereference into a single dereference in the common case, and it was
    // how R3-Alpha did it).
    //
    DS_Movable_Base = ARR_HEAD(DS_Array); // must do before using DS_TOP

    // We fill in the data stack with "GC safe trash" (which is void in the
    // release build, but will raise an alarm if VAL_TYPE() called on it in
    // the debug build).  In order to serve as a marker for the stack slot
    // being available, it merely must not be IS_END()...
    //
    value = DS_TOP;
    len_new = len_old + amount;
    for (n = len_old; n < len_new; ++n) {
        SET_TRASH_SAFE(value);
        ++value;
    }

    // Update the end marker to serve as the indicator for when the next
    // stack push would need to expand.
    //
    SET_ARRAY_LEN(DS_Array, len_new);
    assert(value == ARR_TAIL(DS_Array));
    SET_END(value);

    ASSERT_ARRAY(DS_Array);
}


//
//  Pop_Stack_Values: C
//
// Pops computed values from the stack to make a new ARRAY.
//
REBARR *Pop_Stack_Values(REBDSP dsp_start)
{
    REBCNT len = DSP - dsp_start;
    REBVAL *values = ARR_AT(DS_Array, dsp_start + 1);

    REBARR *array = Copy_Values_Len_Shallow(values, len);

    DS_DROP_TO(dsp_start);
    return array;
}


//
//  Pop_Stack_Values_Into: C
//
// Pops computed values from the stack into an existing ANY-ARRAY.  The
// index of that array will be updated to the insertion tail (/INTO protocol)
//
void Pop_Stack_Values_Into(REBVAL *into, REBDSP dsp_start) {
    REBCNT len = DSP - dsp_start;
    REBVAL *values = ARR_AT(DS_Array, dsp_start + 1);

    assert(ANY_ARRAY(into));
    FAIL_IF_LOCKED_ARRAY(VAL_ARRAY(into));

    VAL_INDEX(into) = Insert_Series(
        ARR_SERIES(VAL_ARRAY(into)),
        VAL_INDEX(into),
        cast(REBYTE*, values),
        len // multiplied by width (sizeof(REBVAL)) in Insert_Series
    );

    DS_DROP_TO(dsp_start);
}


//
//  Push_Ended_Trash_Chunk: C
//
// This doesn't necessarily call Alloc_Mem, because chunks are allocated
// sequentially inside of "chunker" blocks, in their ordering on the stack.
// Allocation is only required if we need to step into a new chunk (and even
// then only if we aren't stepping into a chunk that we are reusing from
// a prior expansion).
//
// The "Ended" indicates that there is no need to manually put an end in the
// `num_values` slot.  Chunks are implicitly terminated by their layout,
// because the low bit of subsequent chunks is set to 0, for data that does
// double-duty as a END marker.
//
REBVAL* Push_Ended_Trash_Chunk(REBCNT num_values) {
    const REBCNT size = BASE_CHUNK_SIZE + num_values * sizeof(REBVAL);

    // an extra Reb_Value_Header is placed at the very end of the array to
    // denote a block terminator without a full REBVAL
    const REBCNT size_with_terminator = size + sizeof(struct Reb_Value_Header);

    struct Reb_Chunker *chunker = CHUNKER_FROM_CHUNK(TG_Top_Chunk);

    struct Reb_Chunk *chunk;

    // Establish invariant where 'chunk' points to a location big enough to
    // hold the data (with data's size accounted for in chunk_size).  Note
    // that TG_Top_Chunk is never NULL, due to the initialization leaving
    // one empty chunk at the beginning and manually destroying it on
    // shutdown (this simplifies Push)
    const REBCNT payload_left = chunker->size - TG_Top_Chunk->offset
          - TG_Top_Chunk->size.bits;

    assert(chunker->size >= CS_CHUNKER_PAYLOAD);

    if (payload_left >= size_with_terminator) {
        //
        // Topmost chunker has space for the chunk *and* a pointer with the
        // END marker bit (e.g. last bit 0).  So advance past the topmost
        // chunk (whose size will depend upon num_values)
        //
        chunk = cast(struct Reb_Chunk*,
            cast(REBYTE*, TG_Top_Chunk) + TG_Top_Chunk->size.bits
        );

        // top's offset accounted for previous chunk, account for ours
        //
        chunk->offset = TG_Top_Chunk->offset + TG_Top_Chunk->size.bits;
    }
    else {
        //
        // Topmost chunker has insufficient space
        //

        REBOOL need_alloc = TRUE;
        if (chunker->next) {
            //
            // Previously allocated chunker exists already, check if it is big
            // enough
            //
            assert(!chunker->next->next);
            if (chunker->next->size >= size_with_terminator)
                need_alloc = FALSE;
            else
                Free_Mem(chunker->next, chunker->next->size + BASE_CHUNKER_SIZE);
        }
        if (need_alloc) {
            // No previously allocated chunker...we have to allocate it
            //
            const REBCNT payload_size = BASE_CHUNKER_SIZE
                + (size_with_terminator < CS_CHUNKER_PAYLOAD ?
                    CS_CHUNKER_PAYLOAD : (size_with_terminator << 1));
            chunker->next = cast(struct Reb_Chunker*, Alloc_Mem(payload_size));
            chunker->next->next = NULL;
            chunker->next->size = payload_size - BASE_CHUNKER_SIZE;
        }

        assert(chunker->next->size >= size_with_terminator);

        chunk = cast(struct Reb_Chunk*, &chunker->next->payload);
        chunk->offset = 0;
    }

    // The size does double duty to terminate the previous chunk's REBVALs
    // so that a full-sized REBVAL that is largely empty isn't needed to
    // convey IS_END().  It must yield its lowest two bits as zero to serve
    // this purpose, so WRITABLE_MASK_DEBUG and NOT_END_MASK will both
    // be false.  Our chunk should be a multiple of 4 bytes in total size,
    // but check that here with an assert.
    //
    // The memory address for the chunk size matches that of a REBVAL's
    // `header`, but since a `struct Reb_Chunk` is distinct from a REBVAL
    // they won't necessarily have write/read coherence, even though the
    // fields themselves are the same type.  Taking the address of the size
    // creates a pointer, which without a `restrict` keyword is defined as
    // being subject to "aliasing".  Hence a write to the pointer could affect
    // *any* other value of that type.  This is necessary for the trick.
    {
        struct Reb_Value_Header *alias = &chunk->size;
        assert(size % 4 == 0);
        alias->bits = size;
    }

    // Set size also in next element to 0, so it can serve as a terminator
    // for the data range of this until it gets its real size (if ever)
    {
        // See note above RE: aliasing.
        //
        struct Reb_Value_Header *alias = &cast(
            struct Reb_Chunk*,
            cast(REBYTE*, chunk) + size)->size;
        alias->bits = 0;
        assert(IS_END(&chunk->values[num_values]));
    }

    chunk->prev = TG_Top_Chunk;

    TG_Top_Chunk = chunk;

#if !defined(NDEBUG)
    //
    // In debug builds we make sure we put in GC-unsafe trash in the chunk.
    // This helps make sure that the caller fills in the values before a GC
    // ever actually happens.  (We could set it to void or something
    // GC-safe, but that might wind up being wasted work if unset is not
    // what the caller was wanting...so leave it to them.)
    {
        REBCNT index;
        for (index = 0; index < num_values; index++)
            INIT_CELL_WRITABLE_IF_DEBUG(&chunk->values[index]);
    }
#endif

    assert(CHUNK_FROM_VALUES(&chunk->values[0]) == chunk);
    return &chunk->values[0];
}


//
//  Drop_Chunk: C
//
// Free an array of previously pushed REBVALs that are protected by GC.  This
// only occasionally requires an actual call to Free_Mem(), due to allocating
// call these arrays sequentially inside of chunks in memory.
//
void Drop_Chunk(REBVAL *opt_head)
{
    struct Reb_Chunk* chunk = TG_Top_Chunk;

    // Passing in `values` is optional, but a good check to make sure you are
    // actually dropping the chunk you think you are.  (On an error condition
    // when dropping chunks to try and restore the top chunk to a previous
    // state, this information isn't available.)
    //
    assert(!opt_head || CHUNK_FROM_VALUES(opt_head) == chunk);

    // Drop to the prior top chunk
    TG_Top_Chunk = chunk->prev;

    if (chunk->offset == 0) {
        // This chunk sits at the head of a chunker.

        struct Reb_Chunker *chunker = CHUNKER_FROM_CHUNK(chunk);

        assert(TG_Top_Chunk);

        // When we've completely emptied a chunker, we check to see if the
        // chunker after it is still live.  If so, we free it.  But we
        // want to keep *this* just-emptied chunker alive for overflows if we
        // rapidly get another push, to avoid Make_Mem()/Free_Mem() costs.

        if (chunker->next) {
            Free_Mem(chunker->next, chunker->next->size + BASE_CHUNKER_SIZE);
            chunker->next = NULL;
        }
    }

    // In debug builds we poison the memory for the chunk... but not the `prev`
    // pointer because we expect that to stick around!
    //
#if !defined(NDEBUG)
    memset(
        cast(REBYTE*, chunk) + sizeof(struct Reb_Chunk*),
        0xBD,
        chunk->size.bits - sizeof(struct Reb_Chunk*)
    );
    assert(IS_END(cast(REBVAL*, chunk)));
#endif
}


//
//  Push_Or_Alloc_Args_For_Underlying_Func: C
//
// Allocate the series of REBVALs inspected by a function when executed (the
// values behind D_ARG(1), D_REF(2), etc.)
//
// If the function is a specialization, then the parameter list of that
// specialization will have *fewer* parameters than the full function would.
// For this reason we push the arguments for the "underlying" function.
// Yet if there are specialized values, they must be filled in from the
// exemplar frame.
//
//
void Push_Or_Alloc_Args_For_Underlying_Func(struct Reb_Frame *f) {
    REBVAL *slot;
    REBCNT num_slots;
    REBARR *varlist;
    REBVAL *special_arg;

    // We need the actual REBVAL of the function here, and not just the REBFUN.
    // This is true even though you can get a canon REBVAL from a function
    // pointer with FUNC_VALUE().  The reason is because all definitional
    // returns share a common REBFUN, and it's only the "hacked" REBVAL that
    // contains the extra information of the exit_from...either in the
    // frame context (if a specialization) or in place of code pointer (if not)
    //
    assert(IS_FUNCTION(f->param));

    f->func = VAL_FUNC(f->param);

    if (VAL_FUNC_CLASS(f->param) == FUNC_CLASS_SPECIALIZED) {
        //
        // Can't use a specialized f->func as the frame's function because
        // it has the wrong number of arguments (calls to VAL_FUNC_PARAMLIST
        // on f->func would be bad).
        //
        if (f->func == VAL_FUNC(f->param))
            f->func = VAL_FUNC(
                CTX_FRAME_FUNC_VALUE(f->param->payload.function.impl.special)
            );

        // !!! For debugging, it would probably be desirable to indicate
        // that this call of the function originated from a specialization.
        // So that would mean saving the specialization's f->func somewhere.

        special_arg = CTX_VARS_HEAD(f->param->payload.function.impl.special);

        // Need to dig f->param a level deeper to see if it's a definitionally
        // scoped RETURN or LEAVE.
        //
        f->param =
            CTX_FRAME_FUNC_VALUE(f->param->payload.function.impl.special);

        f->flags |= DO_FLAG_EXECUTE_FRAME;
    }
    else {
        special_arg = NULL;
    }

    if (
        VAL_FUNC(f->param) == NAT_FUNC(leave)
        || VAL_FUNC(f->param) == NAT_FUNC(return)
    ) {
        f->exit_from = VAL_FUNC_EXIT_FROM(f->param);
    } else
        f->exit_from = NULL;

    // `num_vars` is the total number of elements in the series, including the
    // function's "Self" REBVAL in the 0 slot.
    //
    num_slots = FUNC_NUM_PARAMS(f->func);

    assert(NOT(f->flags & DO_FLAG_HAS_VARLIST)); // should be clear

    // Make REBVALs to hold the arguments.  It will always be at least one
    // slot long, because function frames start with the value of the
    // function in slot 0.
    //
    if (IS_FUNC_DURABLE(FUNC_VALUE(f->func))) {
        //
        // !!! In the near term, it's hoped that CLOSURE! will go away and
        // that stack frames can be "hybrids" with some pooled allocated
        // vars that survive a call, and some that go away when the stack
        // frame is finished.  The groundwork for this is laid but it's not
        // quite ready--so the classic interpretation is that it's all or
        // nothing... CLOSURE!'s variables args and locals all survive the
        // end of the call, and none of a FUNCTION!'s do.
        //
        REBARR *varlist = Make_Array(num_slots + 1);
        SET_ARRAY_LEN(varlist, num_slots + 1);
        SET_END(ARR_AT(varlist, num_slots + 1));
        SET_ARR_FLAG(varlist, SERIES_FLAG_FIXED_SIZE);

        // Skip the [0] slot which will be filled with the CTX_VALUE
        // !!! Note: Make_Array made the 0 slot an end marker
        //
        SET_TRASH_IF_DEBUG(ARR_AT(varlist, 0));
        slot = ARR_AT(varlist, 1);

        f->data.varlist = varlist;
        f->flags |= DO_FLAG_HAS_VARLIST;
    }
    else {
        // We start by allocating the data for the args and locals on the chunk
        // stack.  However, this can be "promoted" into being the data for a
        // frame context if it becomes necessary to refer to the variables
        // via words or an object value.  That object's data will still be this
        // chunk, but the chunk can be freed...so the words can't be looked up.
        //
        // Note that chunks implicitly have an END at the end; no need to
        // put one there.
        //
        REBVAL *stackvars = Push_Ended_Trash_Chunk(num_slots);
        assert(CHUNK_LEN_FROM_VALUES(stackvars) == num_slots);
        slot = &stackvars[0];

        f->data.stackvars = stackvars;
    }

    // Make_Call does not fill the args in the frame--that's up to Do_Core
    // and Apply_Block as they go along.  But the frame has to survive
    // Recycle() during arg fulfillment, slots can't be left uninitialized.
    // It is important to set to void for bookkeeping so that refinement
    // scanning knows when it has filled a refinement slot (and hence its
    // args) or not.
    //
    // !!! Filling with specialized args could be done via a memcpy; doing
    // an unset only writes 1 out of 4 pointer-sized values in release build
    // so maybe faster than a memset (if unsets were the pattern of a uniform
    // byte, currently not true)
    //
    while (num_slots) {
        //
        // In Rebol2 and R3-Alpha, unused refinement arguments were set to
        // NONE! (and refinements were TRUE as opposed to the WORD! of the
        // refinement itself).  We captured the state of the legacy flag at
        // the time of function creation, so that both kinds of functions
        // can coexist at the same time.
        //
        if (special_arg) {
            *slot = *special_arg;
            ++special_arg;
        }
        else
            SET_VOID(slot); // void means unspecialized, fulfill from callsite

        slot++;
        --num_slots;
    }
}


//
//  Context_For_Frame_May_Reify_Core: C
//
// A Reb_Frame does not allocate a REBSER for its frame to be used in the
// context by default.  But one can be allocated on demand, even for a NATIVE!
// in order to have a binding location for the debugger (for instance).
// If it becomes necessary to create words bound into the frame that is
// another case where the frame needs to be brought into existence.
//
// If there's already a frame this will return it, otherwise create it.
//
// The result of this operation will not necessarily give back a managed
// context.  All cases can't be managed because it may be in a partial state
// (of fulfilling function arguments), and may contain bad data in the varlist.
// But if it has already been managed, it will be returned that way.
//
REBCTX *Context_For_Frame_May_Reify_Core(struct Reb_Frame *f) {
    assert(f->eval_type == ET_FUNCTION); // varargs reifies while still pending

    REBCTX *context;
    struct Reb_Chunk *chunk;

    if (f->flags & DO_FLAG_HAS_VARLIST) {
        if (GET_ARR_FLAG(f->data.varlist, ARRAY_FLAG_CONTEXT_VARLIST))
            return AS_CONTEXT(f->data.varlist); // already a context!

        // We have our function call's args in an array, but it is not yet
        // a context.  !!! Really this cannot reify if we're in arg gathering
        // mode, calling MANAGE_ARRAY is illegal -- need test for that !!!

        assert(IS_TRASH_DEBUG(ARR_AT(f->data.varlist, 0))); // we fill this in
        assert(GET_ARR_FLAG(f->data.varlist, SERIES_FLAG_HAS_DYNAMIC));

        context = AS_CONTEXT(f->data.varlist);
        CTX_STACKVARS(context) = NULL;
    }
    else {
        context = AS_CONTEXT(Make_Series(
            1, // length report will not come from this, but from end marker
            sizeof(REBVAL),
            MKS_NO_DYNAMIC // use the REBVAL in the REBSER--no allocation
        ));

        assert(!GET_ARR_FLAG(AS_ARRAY(context), SERIES_FLAG_HAS_DYNAMIC));
        SET_ARR_FLAG(AS_ARRAY(context), SERIES_FLAG_ARRAY);
        SET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_FIXED_SIZE);

        SET_CTX_FLAG(context, CONTEXT_FLAG_STACK);
        SET_CTX_FLAG(context, SERIES_FLAG_ACCESSIBLE);

        CTX_STACKVARS(context) = f->data.stackvars;

        f->data.varlist = CTX_VARLIST(context);
        f->flags |= DO_FLAG_HAS_VARLIST;
    }

    SET_ARR_FLAG(CTX_VARLIST(context), ARRAY_FLAG_CONTEXT_VARLIST);

    // We do not Manage_Context, because we are reusing a word series here
    // that has already been managed.  The arglist array was managed when
    // created and kept alive by Mark_Call_Frames
    //
    INIT_CTX_KEYLIST_SHARED(context, FUNC_PARAMLIST(f->func));
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(context));

    // We do not manage the varlist, because we'd like to be able to free
    // it *if* nothing happens that causes it to be managed.  Note that
    // initializing word REBVALs that are bound into it will ensure
    // managedness, as will creating a REBVAL for it.
    //
    assert(!GET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_MANAGED));

    // When in ET_FUNCTION, the arglist will be marked safe from GC.
    // It is managed because the pointer makes its way into bindings that
    // ANY-WORD! values may have, and they need to not crash.
    //
    // !!! Note that theoretically pending mode arrays do not need GC
    // access as no running code could get them, but the debugger is
    // able to access this information.  This is under review for how it
    // might be stopped.
    //
    VAL_RESET_HEADER(CTX_VALUE(context), REB_FRAME);
    INIT_VAL_CONTEXT(CTX_VALUE(context), context);
    INIT_CONTEXT_FRAME(context, f);

    // A reification of a frame for native code should not allow changing
    // the values out from under it, because that could cause it to crash
    // the interpreter.  (Generally speaking, modification should only be
    // possible in the debugger anyway.)  For now, protect unless it's a
    // user function.
    //
    if (VAL_FUNC_CLASS(FUNC_VALUE(f->func)) != FUNC_CLASS_USER)
        SET_ARR_FLAG(AS_ARRAY(context), SERIES_FLAG_LOCKED);

    return context;
}


//
//  Context_For_Frame_May_Reify_Managed: C
//
REBCTX *Context_For_Frame_May_Reify_Managed(struct Reb_Frame *f)
{
    assert(f->eval_type == ET_FUNCTION);
    assert(!Is_Function_Frame_Fulfilling(f));

    REBCTX *context = Context_For_Frame_May_Reify_Core(f);
    ENSURE_ARRAY_MANAGED(CTX_VARLIST(context));

    // Finally we mark the flags to say this contains a valid frame, so that
    // future calls to this routine will return it instead of making another.
    // This flag must be cleared when the call is finished (as the Reb_Frame
    // will be blown away if there's an error, no concerns about that).
    //
    ASSERT_CONTEXT(context);
    return context;
}


//
//  Drop_Function_Args_For_Frame_Core: C
//
// This routine needs to be shared with the error handling code.  It would be
// nice if it were inlined into Do_Core...but repeating the code just to save
// the function call overhead is second-guessing the optimizer and would be
// a cause of bugs.
//
// Note that in response to an error, we do not want to drop the chunks,
// because there are other clients of the chunk stack that may be running.
// Hence the chunks will be freed by the error trap helper.
//
void Drop_Function_Args_For_Frame_Core(struct Reb_Frame *f, REBOOL drop_chunks)
{
    if (NOT(f->flags & DO_FLAG_HAS_VARLIST)) {
        //
        // Stack extent arguments with no identifying frame (this would
        // be the typical case when calling a native, for instance).
        //
        f->flags &= ~DO_FLAG_EXECUTE_FRAME;
        if (drop_chunks)
            Drop_Chunk(f->data.stackvars);
        return;
    }

    // We're freeing the varlist (or leaving it up to the GC), so clear flag
    //
    f->flags &= ~(DO_FLAG_HAS_VARLIST | DO_FLAG_EXECUTE_FRAME);

    REBARR *varlist = f->data.varlist;
    assert(GET_ARR_FLAG(varlist, SERIES_FLAG_ARRAY));

    if (NOT(GET_ARR_FLAG(varlist, SERIES_FLAG_MANAGED))) {
        //
        // It's an array, but hasn't become managed yet...either because
        // it couldn't be (args still being fulfilled, may have bad cells) or
        // didn't need to be (no Context_For_Frame_May_Reify_Managed).  We
        // can just free it.
        //
        Free_Array(f->data.varlist);
        return;
    }

    // The varlist might have been for indefinite extent variables, or it
    // might be a stub holder for a stack context.

    ASSERT_ARRAY_MANAGED(varlist);

    if (NOT(GET_ARR_FLAG(varlist, CONTEXT_FLAG_STACK))) {
        //
        // If there's no stack memory being tracked by this context, it
        // has dynamic memory and is being managed by the garbage collector
        // so there's nothing to do.
        //
        assert(GET_ARR_FLAG(varlist, SERIES_FLAG_HAS_DYNAMIC));
        return;
    }

    // It's reified but has its data pointer into the chunk stack, which
    // means we have to free it and mark the array inaccessible.

    assert(GET_ARR_FLAG(varlist, ARRAY_FLAG_CONTEXT_VARLIST));
    assert(NOT(GET_ARR_FLAG(varlist, SERIES_FLAG_HAS_DYNAMIC)));

    assert(GET_ARR_FLAG(varlist, SERIES_FLAG_ACCESSIBLE));
    CLEAR_ARR_FLAG(varlist, SERIES_FLAG_ACCESSIBLE);

    if (drop_chunks)
        Drop_Chunk(CTX_STACKVARS(AS_CONTEXT(varlist)));

#if !defined(NDEBUG)
    //
    // The general idea of the "canon" values inside of ANY-CONTEXT!
    // and ANY-FUNCTION! at their slot [0] positions of varlist and
    // paramlist respectively was that all REBVAL instances of that
    // context or object would mirror those bits.  Because we have
    // SERIES_FLAG_ACCESSIBLE then it's possible to keep this invariant
    // and let a stale stackvars pointer be bad inside the context to
    // match any extant REBVALs, but debugging will be more obvious if
    // the bits are deliberately set to bad--even if this is incongruous
    // with those values.  Thus there is no check that these bits line
    // up and we turn the ones in the context itself to garbage here.
    //
    CTX_STACKVARS(AS_CONTEXT(varlist)) = cast(REBVAL*, 0xDECAFBAD);
#endif
}


#if !defined(NDEBUG)

//
//  FRM_ARG_Debug: C
// 
// Debug-only version of getting a variable out of a call
// frame, which asserts if you use an index that is higher
// than the number of arguments in the frame.
//
REBVAL *FRM_ARG_Debug(struct Reb_Frame *frame, REBCNT n)
{
    assert(n != 0 && n <= FRM_NUM_ARGS(frame));
    return &frame->arg[n - 1];
}

#endif
