/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  c-do.c
**  Summary: the core interpreter - the heart of REBOL
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**    WARNING WARNING WARNING
**    This is highly tuned code that should only be modified by experts
**    who fully understand its design. It is very easy to create odd
**    side effects so please be careful and extensively test all changes!
**
***********************************************************************/

#include "sys-core.h"

void Do_Rebcode(const REBVAL *v) {;}

#include "tmp-evaltypes.h"


/***********************************************************************
**
*/  REBINT Eval_Depth(void)
/*
***********************************************************************/
{
	REBINT depth = 0;
	struct Reb_Call *call;

	for (call = DSF; call != NULL; call = PRIOR_DSF(call), depth++);
	return depth;
}


/***********************************************************************
**
*/	struct Reb_Call *Stack_Frame(REBCNT n)
/*
***********************************************************************/
{
	struct Reb_Call *call = DSF;

	for (call = DSF; call != NULL; call = PRIOR_DSF(call)) {
		if (n-- <= 0) return call;
	}

	return NULL;
}


/***********************************************************************
**
*/  REBNATIVE(trace)
/*
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);

	Check_Security(SYM_DEBUG, POL_READ, 0);

	// The /back option: ON and OFF, or INTEGER! for # of lines:
	if (D_REF(2)) { // /back
		if (IS_LOGIC(arg)) {
			Enable_Backtrace(VAL_LOGIC(arg));
		}
		else if (IS_INTEGER(arg)) {
			Trace_Flags = 0;
			Display_Backtrace(Int32(arg));
			return R_UNSET;
		}
	}
	else Enable_Backtrace(FALSE);

	// Set the trace level:
	if (IS_LOGIC(arg)) {
		Trace_Level = VAL_LOGIC(arg) ? 100000 : 0;
	}
	else Trace_Level = Int32(arg);

	if (Trace_Level) {
		Trace_Flags = 1;
		if (D_REF(3)) SET_FLAG(Trace_Flags, 1); // function
		Trace_Depth = Eval_Depth() - 1; // subtract current TRACE frame
	}
	else Trace_Flags = 0;

	return R_UNSET;
}

static REBINT Init_Depth(void)
{
	// Check the trace depth is ok:
	int depth = Eval_Depth() - Trace_Depth;
	if (depth < 0 || depth >= Trace_Level) return -1;
	if (depth > 10) depth = 10;
	Debug_Space(4 * depth);
	return depth;
}

#define CHECK_DEPTH(d) if ((d = Init_Depth()) < 0) return;\

void Trace_Line(REBSER *block, REBINT index, const REBVAL *value)
{
	int depth;

	if (GET_FLAG(Trace_Flags, 1)) return; // function
	if (ANY_FUNC(value)) return;

	CHECK_DEPTH(depth);

	Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,1)), index+1, value);
	if (IS_WORD(value) || IS_GET_WORD(value)) {
		value = GET_VAR(value);
		if (VAL_TYPE(value) < REB_NATIVE)
			Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,2)), value);
		else if (VAL_TYPE(value) >= REB_NATIVE && VAL_TYPE(value) <= REB_FUNCTION)
			Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,3)), Get_Type_Name(value), List_Func_Words(value));
		else
			Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,4)), Get_Type_Name(value));
	}
	/*if (ANY_WORD(value)) {
		word = value;
		if (IS_WORD(value)) value = GET_VAR(word);
		Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,2)), VAL_WORD_FRAME(word), VAL_WORD_INDEX(word), Get_Type_Name(value));
	}
	if (Trace_Stack) Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,3)), DSP, DSF);
	else
	*/
	Debug_Line();
}

void Trace_Func(const REBVAL *word, const REBVAL *value)
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,5)), Get_Word_Name(word), Get_Type_Name(value));
	if (GET_FLAG(Trace_Flags, 1))
		Debug_Values(DSF_ARG(DSF, 1), DSF_NUM_ARGS(DSF), 20);
	else Debug_Line();
}

void Trace_Return(const REBVAL *word, const REBVAL *value)
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,6)), Get_Word_Name(word));
	Debug_Values(value, 1, 50);
}

void Trace_Arg(REBINT num, const REBVAL *arg, const REBVAL *path)
{
	int depth;
	if (IS_REFINEMENT(arg) && (!path || IS_END(path))) return;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,6)), num+1, arg);
}


/***********************************************************************
**
*/	void Trace_Value(REBINT n, const REBVAL *value)
/*
***********************************************************************/
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,n)), value);
}

/***********************************************************************
**
*/	void Trace_String(REBINT n, const REBYTE *str, REBINT limit)
/*
***********************************************************************/
{
	static char tracebuf[64];
	int depth;
	int len = MIN(60, limit);
	CHECK_DEPTH(depth);
	memcpy(tracebuf, str, len);
	tracebuf[len] = '\0';
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,n)), tracebuf);
}


/***********************************************************************
**
*/	void Trace_Error(const REBVAL *value)
/*
***********************************************************************/
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE, 10)), &VAL_ERR_VALUES(value)->type, &VAL_ERR_VALUES(value)->id);
}


/***********************************************************************
**
*/	void Next_Path(REBPVS *pvs)
/*
**		Evaluate next part of a path.
**
***********************************************************************/
{
	REBVAL *path;
	REBPEF func;
	REBVAL temp;

	// Path must have dispatcher, else return:
	func = Path_Dispatch[VAL_TYPE(pvs->value)];
	if (!func) return; // unwind, then check for errors

	pvs->path++;

	//Debug_Fmt("Next_Path: %r/%r", pvs->path-1, pvs->path);

	// object/:field case:
	if (IS_GET_WORD(path = pvs->path)) {
		pvs->select = GET_MUTABLE_VAR(path);
		if (IS_UNSET(pvs->select))
			raise Error_1(RE_NO_VALUE, path);
	}
	// object/(expr) case:
	else if (IS_PAREN(path)) {

		if (Do_Block_Throws(&temp, VAL_SERIES(path), 0)) {
			*pvs->value = temp;
			return;
		}

		pvs->select = &temp;
	}
	else // object/word and object/value case:
		pvs->select = path;

	// Uses selector on the value.
	// .path - must be advanced as path is used (modified by func)
	// .value - holds currently evaluated path value (modified by func)
	// .select - selector on value
    // .store - storage (usually TOS) for constructed values
	// .setval - non-zero for SET-PATH (set to zero after SET is done)
	// .orig - original path for error messages
	switch (func(pvs)) {
	case PE_OK:
		break;
	case PE_SET: // only sets if end of path
		if (pvs->setval && IS_END(pvs->path+1)) {
			*pvs->value = *pvs->setval;
			pvs->setval = 0;
		}
		break;
	case PE_NONE:
		SET_NONE(pvs->store);
	case PE_USE:
		pvs->value = pvs->store;
		break;
	case PE_BAD_SELECT:
		raise Error_2(RE_INVALID_PATH, pvs->orig, pvs->path);
	case PE_BAD_SET:
		raise Error_2(RE_BAD_PATH_SET, pvs->orig, pvs->path);
	case PE_BAD_RANGE:
		raise Error_Out_Of_Range(pvs->path);
	case PE_BAD_SET_TYPE:
		raise Error_2(RE_BAD_FIELD_SET, pvs->path, Type_Of(pvs->setval));
	}

	if (NOT_END(pvs->path+1)) Next_Path(pvs);
}


/***********************************************************************
**
*/	REBVAL *Do_Path(REBVAL *out, const REBVAL **path_val, REBVAL *val)
/*
**		Evaluate a path value. Path_val is updated so
**		result can be used for function refinements.
**		If val is not zero, then this is a SET-PATH.
**		Returns value only if result is a function,
**		otherwise the result is on TOS.
**
***********************************************************************/
{
	REBPVS pvs;

	// None of the values passed in can live on the data stack, because
	// they might be relocated during the path evaluation process.
	//
	assert(!IN_DATA_STACK(out));
	assert(!IN_DATA_STACK(*path_val));
	assert(!val || !IN_DATA_STACK(val));

	// Not currently robust for reusing passed in path or value as the output
	assert(out != *path_val && out != val);

	assert(!val || !THROWN(val));

	pvs.setval = val;		// Set to this new value
	pvs.store = out;		// Space for constructed results

	// Get first block value:
	pvs.orig = *path_val;
	pvs.path = VAL_BLK_DATA(pvs.orig);

	// Lookup the value of the variable:
	if (IS_WORD(pvs.path)) {
		pvs.value = GET_MUTABLE_VAR(pvs.path);
		if (IS_UNSET(pvs.value))
			raise Error_1(RE_NO_VALUE, pvs.path);
	}
	else pvs.value = pvs.path;

	// Start evaluation of path:
	if (IS_END(pvs.path + 1)) {
		// If it was a single element path, return the value rather than
		// try to dispatch it (would cause a crash at time of writing)
		//
		// !!! Is this the desired behavior, or should it be an error?
	}
	else if (Path_Dispatch[VAL_TYPE(pvs.value)]) {
		Next_Path(&pvs);
		// Check for errors:
		if (NOT_END(pvs.path+1) && !ANY_FUNC(pvs.value)) {
			// Only function refinements should get by this line:
			raise Error_2(RE_INVALID_PATH, pvs.orig, pvs.path);
		}
	}
	else if (!ANY_FUNC(pvs.value))
		raise Error_2(RE_BAD_PATH_TYPE, pvs.orig, Type_Of(pvs.value));

	// If SET then we don't return anything
	if (val) {
		return 0;
	} else {
		// If storage was not used, then copy final value back to it:
		if (pvs.value != pvs.store) *pvs.store = *pvs.value;
		// Return 0 if not function or is :path/word...
		if (!ANY_FUNC(pvs.value)) return 0;
		*path_val = pvs.path; // return new path (for func refinements)
		return pvs.value; // only used for functions
	}
}


/***********************************************************************
**
*/	void Pick_Path(REBVAL *out, REBVAL *value, REBVAL *selector, REBVAL *val)
/*
**		Lightweight version of Do_Path used for A_PICK actions.
**		Result on TOS.
**
***********************************************************************/
{
	REBPVS pvs;
	REBPEF func;

	pvs.value = value;
	pvs.path = 0;
	pvs.select = selector;
	pvs.setval = val;
	pvs.store = out;		// Temp space for constructed results

	// Path must have dispatcher, else return:
	func = Path_Dispatch[VAL_TYPE(value)];
	if (!func) return; // unwind, then check for errors

	switch (func(&pvs)) {
	case PE_OK:
		break;
	case PE_SET: // only sets if end of path
		if (pvs.setval) *pvs.value = *pvs.setval;
		break;
	case PE_NONE:
		SET_NONE(pvs.store);
	case PE_USE:
		pvs.value = pvs.store;
		break;
	case PE_BAD_SELECT:
		raise Error_2(RE_INVALID_PATH, pvs.value, pvs.select);
	case PE_BAD_SET:
		raise Error_2(RE_BAD_PATH_SET, pvs.value, pvs.select);
	}
}


/***********************************************************************
**
*/	void Do_Signals(void)
/*
**		Special events to process during evaluation.
**		Search for SET_SIGNAL to find them.
**
***********************************************************************/
{
	REBCNT sigs;
	REBCNT mask;

	// Accumulate evaluation counter and reset countdown:
	if (Eval_Count <= 0) {
		//Debug_Num("Poll:", (REBINT) Eval_Cycles);
		Eval_Cycles += Eval_Dose - Eval_Count;
		Eval_Count = Eval_Dose;
		if (Eval_Limit != 0 && Eval_Cycles > Eval_Limit)
			Check_Security(SYM_EVAL, POL_EXEC, 0);
	}

	if (!(Eval_Signals & Eval_Sigmask)) return;

	// Be careful of signal loops! EG: do not PRINT from here.
	sigs = Eval_Signals & (mask = Eval_Sigmask);
	Eval_Sigmask = 0;	// avoid infinite loop
	//Debug_Num("Signals:", Eval_Signals);

	// Check for recycle signal:
	if (GET_FLAG(sigs, SIG_RECYCLE)) {
		CLR_SIGNAL(SIG_RECYCLE);
		Recycle();
	}

#ifdef NOT_USED_INVESTIGATE
	if (GET_FLAG(sigs, SIG_EVENT_PORT)) {  // !!! Why not used?
		CLR_SIGNAL(SIG_EVENT_PORT);
		Awake_Event_Port();
	}
#endif

	// Escape only allowed after MEZZ boot (no handlers):
	if (GET_FLAG(sigs, SIG_ESCAPE) && PG_Boot_Phase >= BOOT_MEZZ) {
		CLR_SIGNAL(SIG_ESCAPE);
		Eval_Sigmask = mask;
		raise Error_Is(TASK_HALT_ERROR);
	}

	Eval_Sigmask = mask;
}


/***********************************************************************
**
*/	REBOOL Dispatch_Call_Throws(struct Reb_Call *call)
/*
**		Expects call frame to be ready with all arguments fulfilled.
**
***********************************************************************/
{
#if !defined(NDEBUG)
	REBINT dsp_precall = DSP;

	// We keep track of the head of the list of series that are not tracked
	// by garbage collection at the outset of the call.  Then we ensure that
	// when the call is finished, no accumulation has happened.  So all
	// newly allocated series should either be (a) freed or (b) delegated
	// to management by the GC...else they'd represent a leak
	//
	REBCNT manuals_tail = SERIES_TAIL(GC_Manuals);

	const REBYTE *label_str = Get_Word_Name(DSF_LABEL(call));
#endif

	const REBVAL * const func = DSF_FUNC(call);
	REBVAL *out = DSF_OUT(call);

	// We need to save what the DSF was prior to our execution, and
	// cannot simply use our frame's prior...because our frame's
	// prior call frame may be a *pending* frame that we do not want
	// to put in effect when we are finished.
	//
	struct Reb_Call *dsf_precall = DSF;
	SET_DSF(call);

	// Write some garbage (that won't crash the GC) into the `out` slot in
	// the debug build.  This helps to catch functions that do not
	// at some point intentionally write an output value into the slot.
	//
	// Note: if they use that slot for temp space, it subverts this check.
	SET_TRASH_SAFE(out);

	if (Trace_Flags) Trace_Func(DSF_LABEL(call), func);

	switch (VAL_TYPE(func)) {
	case REB_NATIVE:
		Do_Native(func);
		break;
	case REB_ACTION:
		Do_Action(func);
		break;
	case REB_REBCODE:
		Do_Rebcode(func);
		break;
	case REB_COMMAND:
		Do_Command(func);
		break;
	case REB_CLOSURE:
		Do_Closure(func);
		break;
	case REB_FUNCTION:
		Do_Function(func);
		break;
	case REB_ROUTINE:
		Do_Routine(func);
		break;
	default:
		assert(FALSE);
	}

	// Function execution should have written *some* actual output value
	// over the trash that we put in the return slot before the call.
	assert(!IS_TRASH(out));

	assert(VAL_TYPE(out) < REB_MAX); // cheap check

	ASSERT_VALUE_MANAGED(out);

#if !defined(NDEBUG)
	assert(DSP >= dsp_precall);
	if (DSP > dsp_precall) {
		PROBE_MSG(DSF_WHERE(call), "UNBALANCED STACK TRAP!!!");
		panic Error_0(RE_MISC);
	}

	MANUALS_LEAK_CHECK(manuals_tail, cs_cast(label_str));
#endif

	SET_DSF(dsf_precall);
	Free_Call(call);

	return THROWN(out);
}


/***********************************************************************
**
*/	REBCNT Do_Core(REBVAL * const out, REBOOL next, REBSER *block, REBCNT index, REBFLG lookahead)
/*
**		Evaluate the code block until we have:
**			1. An irreducible value (return next index)
**			2. Reached the end of the block (return END_FLAG)
**			3. Encountered an error
**
**		Index is a zero-based index into the block.
**		Op indicates infix operator is being evaluated (precedence);
**		The value (or error) is placed on top of the data stack.
**
**		LOOKAHEAD:
**		When we're in mid-dispatch of an infix function, the precedence
**		is such that we don't want to do further infix lookahead while
**		getting the arguments.  (e.g. with `1 + 2 * 3` we don't want
**		infix `+` to look ahead past the 2 to see the infix `*`)
**
***********************************************************************/
{
#if !defined(NDEBUG)
	REBINT dsp_orig = DSP;

	static int count_static = 0;
	int count;
#endif

	const REBVAL *value;
	REBOOL infix;

	struct Reb_Call *call;

	// Functions don't have "names", though they can be assigned to words.
	// If a function invokes via word lookup (vs. a literal FUNCTION! value),
	// 'label' will be that WORD!, and NULL otherwise.
	const REBVAL *label;

	const REBVAL *refinements;

	// We use the convention that "param" refers to the word from the spec
	// of the function (a.k.a. the "formal" argument) and "arg" refers to
	// the evaluated value the function sees (a.k.a. the "actual" argument)
	REBVAL *param;
	REBVAL *arg;

	// Most of what this routine does can be done with value pointers and
	// the data stack.  Some operations need a unit of additional storage.
	// This is a one-REBVAL-sized cell for saving that data.
	REBVAL save;

	// Though we can protect the value written into the target pointer 'out'
	// from GC during the course of evaluation, we can't protect the
	// underlying value from relocation.  Technically this would be a problem
	// for any series which might be modified while this call is running, but
	// most notably it applies to the data stack--where output used to always
	// be returned.
	//
	assert(!IN_DATA_STACK(out));

do_at_index:
	assert(index != END_FLAG && index != THROWN_FLAG);
	SET_TRASH_SAFE(out);

	// Someday it may be worth it to micro-optimize these null assignments
	// so they don't happen each time through the loop.
	label = NULL;
	refinements = NULL;

#ifndef NDEBUG
	// This counter is helpful for tracking a specific invocation.
	// If you notice a crash, look on the stack for the topmost call
	// and read the count...then put that here and recompile with
	// a breakpoint set.  (The 'count_static' value is captured into a
	// local 'count' so	you still get the right count after recursion.)
	count = ++count_static;
	if (count ==
		// *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
								  0
		// *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***
	) {
		Val_Init_Block_Index(&save, block, index);
		PROBE_MSG(&save, "Do_Core() count trap");
	}
#endif

	//CHECK_MEMORY(1);
	if (C_STACK_OVERFLOWING(&value)) Trap_Stack_Overflow();

	if (--Eval_Count <= 0 || Eval_Signals) Do_Signals();

	value = BLK_SKIP(block, index);
	assert(!THROWN(value));
	ASSERT_VALUE_MANAGED(value);

	if (Trace_Flags) Trace_Line(block, index, value);

	switch (VAL_TYPE(value)) {

	case REB_END:
		SET_UNSET(out);
		return END_FLAG;

	case REB_WORD:
		GET_VAR_INTO(out, value);

	do_fetched_word:
		if (IS_UNSET(out)) raise Error_1(RE_NO_VALUE, value);

		if (ANY_FUNC(out)) {
			// We can only acquire an infix operator's first arg during the
			// "lookahead".  Here we are starting a brand new expression.
			if (VAL_GET_EXT(out, EXT_FUNC_INFIX))
				raise Error_1(RE_NO_OP_ARG, value);

			// We will reuse the TOS for the OUT of the call frame
			label = value;

			value = out;

			if (Trace_Flags) Trace_Line(block, index, value);
			goto do_function_args;
		}
		index++;
		break;

	case REB_SET_WORD:
		index = Do_Core(out, TRUE, block, index + 1, TRUE);

		assert(index != END_FLAG || IS_UNSET(out)); // unset if END_FLAG
		if (IS_UNSET(out)) raise Error_1(RE_NEED_VALUE, value);
		if (index == THROWN_FLAG) goto return_index;

		Set_Var(value, out);
		break;

	case REB_NATIVE:
	case REB_ACTION:
	case REB_REBCODE:
	case REB_COMMAND:
	case REB_CLOSURE:
	case REB_FUNCTION:

		// If we come across an infix function from do_at_index in the loop,
		// we can't actually run it.  It only runs after an evaluation has
		// yielded a value as part of a single "atomic" Do/Next step
		if (VAL_GET_EXT(value, EXT_FUNC_INFIX))
			raise Error_1(RE_NO_OP_ARG, label);

	// Value must be the function when a jump here occurs
	do_function_args:
		assert(ANY_FUNC(value));
		index++;
		assert(DSP == dsp_orig);

		// `out` may contain the pending argument for an infix operation,
		// and it could also be the backing store of the `value` pointer
		// to the function.  So Make_Call() shouldn't overwrite it!
		//
		// Note: Although we create the call frame here, we cann't "put
		// it into effect" until all the arguments have been computed.
		// This is because recursive stack-relative bindings would wind
		// up reading variables out of the frame while it is still
		// being built, and that would be bad.

		call = Make_Call(out, block, index, label, value);

		// Make_Call() put a safe copy of the function value into the
		// call frame.  Refresh our value to point to that one (instead of
		// where it was possibly lingering in the `out` slot).

		value = DSF_FUNC(call);
		assert(ANY_FUNC(value));
		infix = VAL_GET_EXT(value, EXT_FUNC_INFIX);

		// If there are no arguments, just skip the next section
		if (DSF_NUM_ARGS(call) == 0) goto function_ready_to_call;

		// We assume you can enumerate both the formal parameters (in the
		// spec) and the actual arguments (in the call frame) using pointer
		// incrementation, that they are both terminated by REB_END, and
		// that there are an equal number of values in both.
		param = VAL_FUNC_PARAM(value, 1);
		arg = DSF_ARG(call, 1);

		// Fetch the first argument from output slot before overwriting
		// !!! Redundant check on REB_PATH branch (knows it's not infix)
		if (infix) {
			assert(index != 0);

			// If func is being called infix, prior evaluation loop has
			// already computed first argument, so it's sitting in `out`
			*arg = *out;
			if (!TYPE_CHECK(param, VAL_TYPE(arg)))
				raise Error_Arg_Type(call, param, Type_Of(arg));

			param++;
			arg++;
		}

		// This loop goes through the parameter and argument slots.  It
		// starts out going in order, BUT note that when processing
		// refinements, this may "jump around".  (This happens if the path
		// generating the call doesn't specify the refinements in the same
		// order as was in the definition.
		//
		for (; NOT_END(param); param++, arg++) {

			switch (VAL_TYPE(param)) {

			case REB_WORD:
				// An ordinary WORD! in the function spec indicates that you
				// would like that argument to be evaluated normally.
				//
				//     >> foo: function [a] [print [{a is} a]
				//
				//     >> foo 1 + 2
				//     a is 3
				//
				index = Do_Core(arg, TRUE, block, index, !infix);
				if (index == THROWN_FLAG) {
					*out = *arg;
					Free_Call(call);
					goto return_index;
				}
				if (index == END_FLAG)
					raise Error_2(RE_NO_ARG, DSF_LABEL(call), param);
				break;

			case REB_GET_WORD:
				// Using a GET-WORD! in the function spec indicates that you
				// would like that argument to be "quoted" sans evaluation.
				//
				//     >> foo: function [:a] [print [{a is} a]
				//
				//     >> foo 1 + 2
				//     a is 1
				//
				//     >> foo (1 + 2)
				//     a is (1 + 2)
				//
				// A special allowance is made that if a function quotes its
				// argument and can the parameter is at the end of a series,
				// it will be treated as an UNSET!  (This is how HELP manages
				// to act as an arity 1 function as well as an arity 0 one.)
				// But to use this feature it must also explicitly accept
				// the UNSET! type (checked after the switch).
				//
				if (index < BLK_LEN(block)) {
					*arg = *BLK_SKIP(block, index);
					index++;
				}
				else
					SET_UNSET(arg); // series end UNSET! trick
				break;

			case REB_LIT_WORD:
				// Using a LIT-WORD in the function spec indicates that
				// parameters are quoted *unless* they are "gets" or parens.
				//
				//     >> foo: function ['a] [print [{a is} a]
				//
				//     >> foo 1 + 2
				//     a is 1
				//
				//     >> foo (1 + 2)
				//     a is 3
				//
				// This provides a convenient escape mechanism for the caller
				// to subvert quote-like behavior (which is an option that
				// one generally would like to give in a quote-like API).
				//
				// The same trick is allowed for UNSET! at end of series as
				// with a GET-WORD! style quote.
				//
				if (index < BLK_LEN(block)) {
					REBVAL * const quoted = BLK_SKIP(block, index);
					if (
						IS_PAREN(quoted)
						|| IS_GET_WORD(quoted)
						|| IS_GET_PATH(quoted)
					) {
						index = Do_Core(arg, TRUE, block, index, !infix);
						if (index == THROWN_FLAG) {
							*out = *arg;
							Free_Call(call);
							goto return_index;
						}
						if (index == END_FLAG)
							assert(IS_UNSET(arg)); // series end UNSET! trick
					}
					else {
						index++;
						*arg = *quoted;
					}
				} else
					SET_UNSET(arg); // series end UNSET! trick
				break;

			case REB_REFINEMENT:
				// Refinements are tricky because we may hit them in the spec
				// at a time when they are not the
				//
				if (!refinements || IS_END(refinements))
					goto function_ready_to_call;

				if (!IS_WORD(&refinements[0]))
					raise Error_1(RE_BAD_REFINE, &refinements[0]);

				// Optimize, if the refinement is the next arg:
				if (SAME_SYM(&refinements[0], param)) {
					SET_TRUE(arg);
					refinements++;

					// skip type check on refinement itself, and let the
					// loop process its arguments (if any)
					continue;
				}

			seek_refinement:
				// Check is redundant if we are coming from REB_REFINEMENT
				// case above, but not if we're jumping in.
				if (!IS_WORD(&refinements[0]))
					raise Error_1(RE_BAD_REFINE, &refinements[0]);

				param = VAL_FUNC_PARAM(value, 1);
				arg = DSF_ARG(call, 1);
				for (; NOT_END(param); param++, arg++) {
					if (IS_REFINEMENT(param))
						if (SAME_SYM(param, &refinements[0])) {
							SET_TRUE(arg);
							refinements++;
							break; // will fall through to continue below
						}
				}
				// Was refinement found? If not, error:
				if (IS_END(param))
					raise Error_2(RE_NO_REFINE, DSF_LABEL(call), &refinements[0]);

				// skip type check on refinement itself, and let the
				// loop process its arguments (if any)
				continue;

			case REB_SET_WORD:
				// The SET-WORD! is reserved for special features.  Red has
				// used RETURN: as a specifier for the return value, but this
				// may lead to problems with the locals-gathering mechanics
				// with nested FUNCTION declarations.
				raise Error_Invalid_Arg(param);

			default:
				raise Error_Invalid_Arg(param);
			}

			ASSERT_VALUE_MANAGED(arg);

			// If word is typed, verify correct argument datatype:
			if (!TYPE_CHECK(param, VAL_TYPE(arg)))
				raise Error_Arg_Type(call, param, Type_Of(arg));
		}

		// Hack to process remaining path:
		if (refinements && NOT_END(refinements)) goto seek_refinement;

	function_ready_to_call:
		// Execute the function with all arguments ready
		if (Dispatch_Call_Throws(call)) {
			index = THROWN_FLAG;
			goto return_index;
		}

		if (Trace_Flags) Trace_Return(label, out);

		// The return value is a FUNC that needs to be re-evaluated.
		if (ANY_FUNC(out) && VAL_GET_EXT(out, EXT_FUNC_REDO)) {
			if (VAL_GET_EXT(out, EXT_FUNC_INFIX))
				raise Error_Has_Bad_Type(value); // not allowed

			value = out;
			label = NULL;
			index--; // Backup block index to re-evaluate.

			goto do_function_args;
		}
		break;

	case REB_PATH:
		label = value;

		// returns in word the path item, DS_TOP has value
		value = Do_Path(out, &label, 0);
		if (THROWN(out)) {
			index = THROWN_FLAG;
			goto return_index;
		}

		// Value returned only for functions that need evaluation
		if (value && ANY_FUNC(value)) {
			// object/func or func/refinements or object/func/refinement:

			assert(label);

			// You can get an actual function value as a label if you use it
			// literally with a refinement.  Tricky to make it, but possible:
			//
			// do reduce [
			//     to-path reduce [:append 'only] [a] [b]
			// ]
			//
			// Hence legal, but we don't pass that into Make_Call.

			if (!IS_WORD(label) && !ANY_FUNC(label))
				raise Error_1(RE_BAD_REFINE, label); // CC#2226

			// We should only get a label that is the function if said label
			// is the function value itself.
			assert(!ANY_FUNC(label) || value == label);

			// Cannot handle infix because prior value is wiped out above
			// (Theoretically we could save it if we are DO-ing a chain of
			// values, and make it work.  But then, a loop of DO/NEXT
			// may not behave the same as DO-ing the whole block.  Bad.)

			if (VAL_GET_EXT(value, EXT_FUNC_INFIX))
				raise Error_Has_Bad_Type(value);

			refinements = label + 1;

			// It's possible to put a literal function value into a path,
			// but the labeling mechanism currently expects a word or NULL
			// for what you dispatch from.

			if (ANY_FUNC(label))
				label = NULL;

			goto do_function_args;
		} else
			index++;
		break;

	case REB_GET_PATH:
		label = value;

		// returns in word the path item, DS_TOP has value
		value = Do_Path(out, &label, 0);

		// !!! Historically this just ignores a result indicating this is a
		// function with refinements, e.g. ':append/only'.  However that
		// ignoring seems unwise.  It should presumably create a modified
		// function in that case which acts as if it has the refinement.
		if (label && !IS_END(label + 1) && ANY_FUNC(out))
			raise Error_0(RE_TOO_LONG);

		index++;
		break;

	case REB_SET_PATH:
		index = Do_Core(out, TRUE, block, index + 1, TRUE);

		assert(index != END_FLAG || IS_UNSET(out)); // unset if END_FLAG
		if (IS_UNSET(out)) raise Error_1(RE_NEED_VALUE, label);
		if (index == THROWN_FLAG) goto return_index;

		label = value;
		Do_Path(&save, &label, out);
		// !!! No guarantee that result of a set-path eval would put the
		// set value in out atm, so can't reverse this yet so that the
		// first Do is into 'save' and the second into 'out'.  (Review)
		break;

	case REB_PAREN:
		if (Do_Block_Throws(out, VAL_SERIES(value), 0)) {
			index = THROWN_FLAG;
			goto return_index;
		}
		index++;
		break;

	case REB_LIT_WORD:
		*out = *value;
		VAL_SET(out, REB_WORD);
		index++;
		break;

	case REB_GET_WORD:
		GET_VAR_INTO(out, value);
		index++;
		break;

	case REB_LIT_PATH:
		// !!! Aliases a REBSER under two value types, likely bad, see CC#2233
		*out = *value;
		VAL_SET(out, REB_PATH);
		index++;
		break;

	case REB_FRAME:
		// !!! Frame should be hidden from user visibility
		panic Error_1(RE_BAD_EVALTYPE, Get_Type(VAL_TYPE(value)));

	default:
		// Most things just evaluate to themselves
		assert(!IS_TRASH(value));
		*out = *value;
		index++;
		break;
	}

	if (index >= BLK_LEN(block)) goto return_index;

	// Should not have accumulated any net data stack during the evaluation
	assert(DSP == dsp_orig);

	// Should not have a THROWN value if we got here
	assert(index != THROWN_FLAG && !THROWN(out));

	// We do not look ahead for infix dispatch if we are currently processing
	// an infix operation with higher precedence
	if (lookahead) {
		value = BLK_SKIP(block, index);

		// Literal infix function values may occur.
		if (VAL_GET_EXT(value, EXT_FUNC_INFIX)) {
			label = NULL;
			if (Trace_Flags) Trace_Line(block, index, value);
			goto do_function_args;
		}

		if (IS_WORD(value)) {
			// WORD! values may look up to an infix function

			GET_VAR_INTO(&save, value);
			if (VAL_GET_EXT(&save, EXT_FUNC_INFIX)) {
				label = value;
				value = &save;
				if (Trace_Flags) Trace_Line(block, index, value);
				goto do_function_args;
			}

			// Perhaps not an infix function, but we just paid for a variable
			// lookup.  If this isn't just a DO/NEXT, use the work!
			if (!next) {
				*out = save;
				goto do_fetched_word;
			}
		}
	}

	// Continue evaluating rest of block if not just a DO/NEXT
	if (!next) goto do_at_index;

return_index:
	assert(DSP == dsp_orig);
	assert(!IS_TRASH(out));
	assert((index == THROWN_FLAG) == THROWN(out));
	assert(index != END_FLAG || index >= BLK_LEN(block));
	assert(VAL_TYPE(out) < REB_MAX); // cheap check
	return index;
}


/***********************************************************************
**
*/	void Reduce_Block(REBVAL *out, REBSER *block, REBCNT index, REBOOL into)
/*
**		Reduce block from the index position specified in the value.
**		Collect all values from stack and make them a block.
**
***********************************************************************/
{
	REBINT dsp_orig = DSP;

	while (index < BLK_LEN(block)) {
		REBVAL reduced;
		index = Do_Next_May_Throw(&reduced, block, index);
		if (index == THROWN_FLAG) {
			*out = reduced;
			DS_DROP_TO(dsp_orig);
			goto finished;
		}
		DS_PUSH(&reduced);
	}

	Pop_Stack_Values(out, dsp_orig, into);

finished:
	assert(DSP == dsp_orig);
}


/***********************************************************************
**
*/	void Reduce_Only(REBVAL *out, REBSER *block, REBCNT index, REBVAL *words, REBOOL into)
/*
**		Reduce only words and paths not found in word list.
**
***********************************************************************/
{
	REBINT dsp_orig = DSP;
	REBVAL *val;
	const REBVAL *v;
	REBSER *ser = 0;
	REBCNT idx = 0;

	if (IS_BLOCK(words)) {
		ser = VAL_SERIES(words);
		idx = VAL_INDEX(words);
	}

	for (val = BLK_SKIP(block, index); NOT_END(val); val++) {
		if (IS_WORD(val)) {
			// Check for keyword:
			if (ser && NOT_FOUND != Find_Word(ser, idx, VAL_WORD_CANON(val))) {
				DS_PUSH(val);
				continue;
			}
			v = GET_VAR(val);
			DS_PUSH(v);
		}
		else if (IS_PATH(val)) {
			const REBVAL *v;

			if (ser) {
				// Check for keyword/path:
				v = VAL_BLK_DATA(val);
				if (IS_WORD(v)) {
					if (NOT_FOUND != Find_Word(ser, idx, VAL_WORD_CANON(v))) {
						DS_PUSH(val);
						continue;
					}
				}
			}

			v = val;

			// pushes val on stack
			DS_PUSH_TRASH_SAFE;
			Do_Path(DS_TOP, &v, NULL);
		}
		else DS_PUSH(val);
		// No need to check for unwinds (THROWN) here, because unwinds should
		// never be accessible via words or paths.
	}

	Pop_Stack_Values(out, dsp_orig, into);

	assert(DSP == dsp_orig);
}


/***********************************************************************
**
*/	void Reduce_Block_No_Set(REBVAL *out, REBSER *block, REBCNT index, REBOOL into)
/*
***********************************************************************/
{
	REBINT dsp_orig = DSP;

	while (index < BLK_LEN(block)) {
		REBVAL *value = BLK_SKIP(block, index);
		if (IS_SET_WORD(value)) {
			DS_PUSH(value);
			index++;
		}
		else {
			REBVAL reduced;
			index = Do_Next_May_Throw(&reduced, block, index);
			if (index == THROWN_FLAG) {
				*out = reduced;
				DS_DROP_TO(dsp_orig);
				goto finished;
			}
			DS_PUSH(&reduced);
		}
	}

	Pop_Stack_Values(out, dsp_orig, into);

finished:
	assert(DSP == dsp_orig);
}


/***********************************************************************
**
*/	void Reduce_Type_Stack(REBSER *block, REBCNT index, REBCNT type)
/*
**		Reduce a block of words/paths that are of the specified type.
**		Return them on the stack. The change in TOS is the length.
**
***********************************************************************/
{
	//REBINT start = DSP + 1;
	REBVAL *val;

	// Lookup words and paths and push values on stack:
	for (val = BLK_SKIP(block, index); NOT_END(val); val++) {
		if (IS_WORD(val)) {
			const REBVAL *v = GET_VAR(val);
			if (VAL_TYPE(v) == type) DS_PUSH(v);
		}
		else if (IS_PATH(val)) {
			const REBVAL *v = val;
			REBVAL safe;

			if (!Do_Path(&safe, &v, 0)) {
				if (VAL_TYPE(&safe) != type) DS_DROP;
			}
		}
		else if (VAL_TYPE(val) == type) DS_PUSH(val);
		// !!! check stack size
	}
}


/***********************************************************************
**
*/	void Reduce_In_Frame(REBSER *frame, REBVAL *values)
/*
**		Reduce a block with simple lookup in the context.
**		Only words in that context are valid (e.g. error object).
**		All values are left on the stack. No copy is made.
**
***********************************************************************/
{
	REBVAL *val;

	for (; NOT_END(values); values++) {
		switch (VAL_TYPE(values)) {
		case REB_WORD:
		case REB_SET_WORD:
		case REB_GET_WORD:
			if ((val = Find_Word_Value(frame, VAL_WORD_SYM(values)))) {
				DS_PUSH(val);
				break;
			} // Unknown in context, fall below, use word as value.
		case REB_LIT_WORD:
			DS_PUSH(values);
			VAL_SET(DS_TOP, REB_WORD);
			break;
		default:
			DS_PUSH(values);
		}
	}
}


/***********************************************************************
**
*/	void Compose_Block(REBVAL *out, REBVAL *block, REBFLG deep, REBFLG only, REBOOL into)
/*
**		Compose a block from a block of un-evaluated values and
**		paren blocks that are evaluated.  Performs evaluations, so
**		if 'into' is provided, then its series must be protected from
**		garbage collection.
**
**			deep - recurse into sub-blocks
**			only - parens that return blocks are kept as blocks
**
**		Writes result value at address pointed to by out.
**
***********************************************************************/
{
	REBVAL *value;
	REBINT dsp_orig = DSP;

	for (value = VAL_BLK_DATA(block); NOT_END(value); value++) {
		if (IS_PAREN(value)) {
			REBVAL evaluated;

			if (Do_Block_Throws(&evaluated, VAL_SERIES(value), 0)) {
				*out = evaluated;
				DS_DROP_TO(dsp_orig);
				goto finished;
			}

			if (IS_BLOCK(&evaluated) && !only) {
				// compose [blocks ([a b c]) merge] => [blocks a b c merge]
				Push_Stack_Values(
					cast(REBVAL*, VAL_BLK_DATA(&evaluated)),
					VAL_BLK_LEN(&evaluated)
				);
			}
			else if (!IS_UNSET(&evaluated)) {
				// compose [(1 + 2) inserts as-is] => [3 inserts as-is]
				// compose/only [([a b c]) unmerged] => [[a b c] unmerged]
				DS_PUSH(&evaluated);
			}
			else {
				// compose [(print "Unsets *vanish*!")] => []
			}
		}
		else if (deep) {
			if (IS_BLOCK(value)) {
				// compose/deep [does [(1 + 2)] nested] => [does [3] nested]
				REBVAL composed;
				Compose_Block(&composed, value, TRUE, only, into);
				DS_PUSH(&composed);
			}
			else {
				DS_PUSH(value);
				if (ANY_BLOCK(value)) {
					// compose [copy/(orig) (copy)] => [copy/(orig) (copy)]
					// !!! path and second paren are copies, first paren isn't
					VAL_SERIES(DS_TOP) = Copy_Array_Shallow(VAL_SERIES(value));
					MANAGE_SERIES(VAL_SERIES(DS_TOP));
				}
			}
		}
		else {
			// compose [[(1 + 2)] (reverse "wollahs")] => [[(1 + 2)] "shallow"]
			DS_PUSH(value);
		}
	}

	Pop_Stack_Values(out, dsp_orig, into);

finished:
	assert(DSP == dsp_orig);
}


/***********************************************************************
**
*/	REBOOL Apply_Block_Throws(REBVAL *out, const REBVAL *func, REBSER *block, REBCNT index, REBFLG reduce)
/*
**		Use a block at a certain index as the source of parameters to
**		a function invocation.  If 'reduce' then the block will be
**		evaluated in steps via Do_Next_May_Throw and the results passed as
**		the arguments, otherwise it will be taken as literal values.
**
**		Refinements are passed according to their positions relative
**		to the order in which they were defined in the spec.  (Brittle,
**		but that's how it's been done.)  Any conditionally true
**		value in a refinement position means the refinement will be
**		passed as TRUE, while conditional falsehood means NONE.
**		Arguments to an unused refinement will still be evaluated if
**		'reduce' is set, will be passed as NONE.
**
**		The block will be effectively padded with NONE to the number
**		of arguments if it is shorter than the total needed.  If
**		there are more values in the block than arguments, that
**		will be an error.
**
**		returns TRUE if out is THROWN()
**
***********************************************************************/
{
	struct Reb_Call *call;
	REBVAL *arg;
	REBVAL *param;
	REBOOL ignoring = FALSE;
	REBOOL too_many = FALSE;

	SET_TRASH_SAFE(out);

	// !!! Should infix work here, but just act like a normal function?
	// Historically that is how it has worked:
	//
	//     >> apply :+ [1 2]
	//     3
	//
	// Whether that's confusing or sensible depends.

	if (index > SERIES_TAIL(block)) index = SERIES_TAIL(block);

	// Now push function frame
	call = Make_Call(out, block, index, NULL, func);

	// Gather arguments:

	arg = DSF_NUM_ARGS(call) > 0 ? DSF_ARG(call, 1) : END_VALUE;
	if (VAL_FUNC_NUM_PARAMS(func) > 0)
		param = VAL_FUNC_PARAM(func, 1);
	else
		param = END_VALUE;

	while (index < BLK_LEN(block)) {
		if (!too_many && IS_END(param)) {
			too_many = TRUE;
			if (!reduce) break;

			// Semantically speaking, 'apply x y' behaves "as if" you had
			// written 'apply/only x reduce y'.  This means that even if a
			// block contains too many values for the function being called,
			// we can't report that before finishing the reduce.  (e.g.
			// 'apply does [] [1 2 return 3 4]' should return before there
			// is an opportunity to report the too-many-args error)
		}

		// Reduce (or just copy) block content to call frame:
		if (reduce) {
			index = Do_Next_May_Throw(out, block, index);
			if (index == THROWN_FLAG) {
				Free_Call(call);
				return FALSE;
			}
			if (too_many) continue;
			*arg = *out;
		} else {
			assert(!too_many);
			*arg = *BLK_SKIP(block, index);
			index++;
		}

		// If arg is refinement, determine its state:
		if (IS_REFINEMENT(param)) {
			if (IS_CONDITIONAL_TRUE(arg)) {
				// !!! Review this in light of the idea of refinements
				// holding the value of their words
				SET_TRUE(arg);
				ignoring = FALSE;
			} else {
				SET_NONE(arg);
				ignoring = TRUE;
			}
		}
		else {
			if (ignoring)
				SET_NONE(arg);
			else {
				// If arg is typed, verify correct argument datatype:
				if (!TYPE_CHECK(param, VAL_TYPE(arg)))
					raise Error_Arg_Type(call, param, Type_Of(arg));
			}
		}

		arg++;
		param++;
	}

	if (too_many) {
		// With the effective reduction of the block (if it was necessary)
		// now we can report an error about the size.  "Content too long"
		// is probably not the right error; needs a more specific one.
		raise Error_0(RE_TOO_LONG);
	}

	return Dispatch_Call_Throws(call);
}


/***********************************************************************
**
*/	REBOOL Apply_Function_Throws(REBVAL *out, const REBVAL *func, va_list *values)
/*
**		(va_list by pointer: http://stackoverflow.com/a/3369762/211160)
**
**		Applies function from args provided by C call. Zero terminated.
**		Does not type check in release build; assumes the system is
**		calling correctly (Debug build does check)
**
**		out	- result value
**		func - function to call
**		values - values to pass as function args (null terminated)
**
**		!!! OPs are allowed, treated as normal functions.  Good idea?
**
**		returns TRUE if out is THROWN()
**
***********************************************************************/
{
	struct Reb_Call *call;
	REBVAL *param;
	REBVAL *arg;
	REBVAL *value;

	REBOOL ignoring = FALSE;

	REBSER *where_block;
	REBCNT where_index;

	// For debug backtracing, DO wants to know what our execution
	// block and position are.  We have to make something up, because
	// this call is originating from C code (not Rebol code).

	if (DSF) {
		// Some function is on the stack, so fabricate our execution
		// position by copying the block and position it was at.

		where_block = VAL_SERIES(DSF_WHERE(DSF));
		where_index = VAL_INDEX(DSF_WHERE(DSF));
	}
	else if (IS_FUNCTION(func) || IS_CLOSURE(func)) {
		// Stack is empty, so offer up the body of the function itself
		// (if it has a body!)

		where_block = VAL_FUNC_BODY(func);
		where_index = 0;
	}
	else {
		// We got nothin'.  Give back the specially marked "top level"
		// empty block just to provide something in the slot
		// !!! Could use more sophisticated backtracing in general

		where_block = EMPTY_SERIES;
		where_index = 0;
	}

	SET_TRASH_SAFE(out);

	call = Make_Call(out, where_block, where_index, NULL, func);

	param = VAL_FUNC_PARAM(func, 1);
	arg = DSF_ARG(call, 1);

	for (; NOT_END(param); param++, arg++) {
		const REBVAL *value = va_arg(*values, REBVAL*);

		if (!value) break;

		if (THROWN(value)) {
			*out = *value;
			Free_Call(call);
			return FALSE;
		}

		*arg = *value;

	#ifndef NDEBUG
		// !!! Should this be in the release build or "just trust it"?
		// original code had no checking whatsoever.

		if (IS_REFINEMENT(param)) {
			if (IS_LOGIC(arg) && VAL_LOGIC(arg))
				ignoring = FALSE;
			else if (IS_NONE(arg))
				ignoring = TRUE;
			else {
				// !!! Old code did not force to TRUE or NONE.  But functions
				// expect a refinement to be TRUE or NONE.  Should we test for
				// IS_CONDITIONAL_TRUE and give the appropriate value, giving
				// C-invocations the same leeway as APPLY?
				assert(FALSE);
			}
		}
		else if (ignoring)
			assert(IS_NONE(arg)); // !!! again, old code did not force this
		else {
			// If arg is typed, verify correct argument datatype:
			if (!TYPE_CHECK(param, VAL_TYPE(arg)))
				raise Error_Arg_Type(call, param, Type_Of(arg));
		}
	#endif
	}

	return Dispatch_Call_Throws(call);
}


/***********************************************************************
**
*/	REBOOL Apply_Func_Throws(REBVAL *out, REBVAL *func, ...)
/*
**		Applies function from args provided by C call. Zero terminated.
**
**		returns TRUE if out is THROWN()
**
***********************************************************************/
{
	REBOOL result;
	va_list args;

	if (!ANY_FUNC(func)) raise Error_Invalid_Arg(func);

	va_start(args, func);
	result = Apply_Function_Throws(out, func, &args);
	va_end(args);

	return result;
}


/***********************************************************************
**
*/	REBOOL Do_Sys_Func_Throws(REBVAL *out, REBCNT inum, ...)
/*
**		Evaluates a SYS function and out contains the result.
**
***********************************************************************/
{
	REBOOL result;
	va_list args;
	REBVAL *value = FRM_VALUE(Sys_Context, inum);

	if (!ANY_FUNC(value)) raise Error_1(RE_BAD_SYS_FUNC, value);

	va_start(args, inum);
	result = Apply_Function_Throws(out, value, &args);
	va_end(args);

	return result;
}


/***********************************************************************
**
*/	void Do_Construct(REBVAL value[])
/*
**		Do a block with minimal evaluation and no evaluation of
**		functions. Used for things like script headers where security
**		is important.
**
**		Handles cascading set words:  word1: word2: value
**
***********************************************************************/
{
	REBVAL *temp;
	REBINT ssp;  // starting stack pointer

	DS_PUSH_NONE;
	temp = DS_TOP;
	ssp = DSP;

	for (; NOT_END(value); value++) {
		if (IS_SET_WORD(value)) {
			// Next line not needed, because SET words are ALWAYS in frame.
			//if (VAL_WORD_INDEX(value) > 0 && VAL_WORD_FRAME(value) == frame)
				DS_PUSH(value);
		} else {
			// Get value:
			if (IS_WORD(value)) {
				switch (VAL_WORD_CANON(value)) {
				case SYM_NONE:
					SET_NONE(temp);
					break;
				case SYM_TRUE:
				case SYM_ON:
				case SYM_YES:
					SET_TRUE(temp);
					break;
				case SYM_FALSE:
				case SYM_OFF:
				case SYM_NO:
					SET_FALSE(temp);
					break;
				default:
					*temp = *value;
					VAL_SET(temp, REB_WORD);
				}
			}
			else if (IS_LIT_WORD(value)) {
				*temp = *value;
				VAL_SET(temp, REB_WORD);
			}
			else if (IS_LIT_PATH(value)) {
				*temp = *value;
				VAL_SET(temp, REB_PATH);
			}
			else if (VAL_TYPE(value) >= REB_NONE) { // all valid values
				*temp = *value;
			}
			else
				SET_NONE(temp);

			// Set prior set-words:
			while (DSP > ssp) {
				Set_Var(DS_TOP, temp);
				DS_DROP;
			}
		}
	}
	DS_DROP; // temp
}


/***********************************************************************
**
*/	void Do_Min_Construct(REBVAL value[])
/*
**		Do no evaluation of the set values.
**
***********************************************************************/
{
	REBVAL *temp;
	REBINT ssp;  // starting stack pointer

	DS_PUSH_NONE;
	temp = DS_TOP;
	ssp = DSP;

	for (; NOT_END(value); value++) {
		if (IS_SET_WORD(value)) {
			// Next line not needed, because SET words are ALWAYS in frame.
			//if (VAL_WORD_INDEX(value) > 0 && VAL_WORD_FRAME(value) == frame)
				DS_PUSH(value);
		} else {
			// Get value:
			*temp = *value;
			// Set prior set-words:
			while (DSP > ssp) {
				Set_Var(DS_TOP, temp);
				DS_DROP;
			}
		}
	}
	DS_DROP; // temp
}


/***********************************************************************
**
*/	REBOOL Redo_Func_Throws(REBVAL *func_val)
/*
**		Trampoline a function, restacking arguments as needed.
**
**	Setup:
**		The source for arguments is the existing stack frame,
**		or a prior stack frame. (Prep_Func + Args)
**
**		Returns FALSE if result is THROWN()
**
***********************************************************************/
{
	REBSER *wsrc;		// words of source func
	REBSER *wnew;		// words of target func
	REBCNT isrc;		// index position in source frame
	REBCNT inew;		// index position in target frame
	REBVAL *word;
	REBVAL *word2;
	REBINT dsp_orig = DSP;

	struct Reb_Call *call;
	REBVAL *arg;

	wsrc = VAL_FUNC_WORDS(DSF_FUNC(DSF));
	wnew = VAL_FUNC_WORDS(func_val);

	// As part of the "Redo" we are not adding a new function location,
	// label, or place to write the output.  We are substituting new code
	// and perhaps adjusting the arguments in our re-doing call.

	call = Make_Call(
		DSF_OUT(DSF),
		VAL_SERIES(DSF_WHERE(DSF)),
		VAL_INDEX(DSF_WHERE(DSF)),
		DSF_LABEL(DSF),
		func_val
	);

	// Foreach arg of the target, copy to source until refinement.
	arg = DSF_ARG(call, 1);
	for (isrc = inew = FIRST_PARAM_INDEX; inew < BLK_LEN(wnew); inew++, isrc++, arg++) {
		word = BLK_SKIP(wnew, inew);
		if (isrc > BLK_LEN(wsrc)) isrc = BLK_LEN(wsrc);

		switch (VAL_TYPE(word)) {
			case REB_SET_WORD: // !!! for definitional return...
				assert(FALSE); // !!! (but not yet)
			case REB_WORD:
			case REB_LIT_WORD:
			case REB_GET_WORD:
				if (VAL_TYPE(word) == VAL_TYPE(BLK_SKIP(wsrc, isrc))) {
					*arg = *DSF_ARG(DSF, isrc);
					// !!! Should check datatypes for new arg passing!
				}
				else {
					// !!! Why does this allow the bounced-to function to have
					// a different type, and push a none instead of erroring?
					SET_NONE(arg);
				}
				break;

			// At refinement, search for it in source, then continue with words.
			case REB_REFINEMENT:
				// Are we aligned on the refinement already? (a common case)
				word2 = BLK_SKIP(wsrc, isrc);
				if (
					IS_REFINEMENT(word2)
					&& VAL_WORD_CANON(word2) == VAL_WORD_CANON(word)
				) {
					*arg = *DSF_ARG(DSF, isrc);
				}
				else {
					// No, we need to search for it:
					for (isrc = FIRST_PARAM_INDEX; isrc < BLK_LEN(wsrc); isrc++) {
						word2 = BLK_SKIP(wsrc, isrc);
						if (
							IS_REFINEMENT(word2)
							&& VAL_WORD_CANON(word2) == VAL_WORD_CANON(word)
						) {
							*arg = *DSF_ARG(DSF, isrc);
							break;
						}
					}
					// !!! The function didn't have the refinement so skip
					// it.  But what will happen now with the arguments?
					SET_NONE(arg);
					//if (isrc >= BLK_LEN(wsrc)) raise Error_Invalid_Arg(word);
				}
				break;

			default:
				panic Error_0(RE_MISC);
		}
	}

	return Dispatch_Call_Throws(call);
}


/***********************************************************************
**
*/	void Get_Simple_Value_Into(REBVAL *out, const REBVAL *val)
/*
**		Does easy lookup, else just returns the value as is.
**
***********************************************************************/
{
	if (IS_WORD(val) || IS_GET_WORD(val)) {
		GET_VAR_INTO(out, val);
	}
	else if (IS_PATH(val) || IS_GET_PATH(val)) {
		// !!! Temporary: make a copy to pass mutable value to Do_Path
		REBVAL path = *val;
		const REBVAL *v = &path;
		Do_Path(out, &v, 0);
	}
	else {
		*out = *val;
	}
}


/***********************************************************************
**
*/	REBSER *Resolve_Path(REBVAL *path, REBCNT *index)
/*
**		Given a path, return a context and index for its terminal.
**
***********************************************************************/
{
	REBVAL *sel; // selector
	const REBVAL *val;
	REBSER *blk;
	REBCNT i;

	if (VAL_TAIL(path) < 2) return 0;
	blk = VAL_SERIES(path);
	sel = BLK_HEAD(blk);
	if (!ANY_WORD(sel)) return 0;
	val = GET_VAR(sel);

	sel = BLK_SKIP(blk, 1);
	while (TRUE) {
		if (!ANY_OBJECT(val) || !IS_WORD(sel)) return 0;
		i = Find_Word_Index(VAL_OBJ_FRAME(val), VAL_WORD_SYM(sel), FALSE);
		sel++;
		if (IS_END(sel)) {
			*index = i;
			return VAL_OBJ_FRAME(val);
		}
	}

	return 0; // never happens
}
