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
**  Summary: Program and Thread Globals
**  Module:  sys-globals.h
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

//-- Bootstrap variables:
PVAR REBINT PG_Boot_Phase;	// To know how far in the boot we are.
PVAR REBINT PG_Boot_Level;	// User specified startup level
PVAR REBYTE **PG_Boot_Strs;	// Special strings in boot.r (RS_ constants)

//-- Various statistics about memory, etc.
PVAR REB_STATS *PG_Reb_Stats;
PVAR REBU64 PG_Mem_Usage;	// Overall memory used
PVAR REBU64 PG_Mem_Limit;	// Memory limit set by SECURE

//-- Symbol Table:
PVAR REBSER *PG_Word_Names;	// Holds all word strings. Never removed.
PVAR WORD_TABLE PG_Word_Table; // Symbol values accessed by hash

//-- Main contexts:
PVAR ROOT_CTX *Root_Context; // System root variables
PVAR REBSER   *Lib_Context;
PVAR REBSER   *Sys_Context;

//-- Various char tables:
PVAR REBYTE *White_Chars;
PVAR REBUNI *Upper_Cases;
PVAR REBUNI *Lower_Cases;

// Other:
PVAR REBYTE *PG_Pool_Map;	// Memory pool size map (created on boot)
PVAR REBSER *PG_Root_Words;	// Root object word table (reused by threads)

PVAR REBI64 PG_Boot_Time;	// Counter when boot started
PVAR REBINT Current_Year;
PVAR REB_OPTS *Reb_Opts;

/* for memory allocation trouble shooting */
#ifndef NDEBUG
    PVAR REBOOL PG_Always_Malloc;
#endif

// This signal word should be thread-local, but it will not work
// when implemented that way. Needs research!!!!
PVAR REBCNT	Eval_Signals;	// Signal flags



/***********************************************************************
**
**  Thread Globals - Local to each thread
**
***********************************************************************/

TVAR TASK_CTX *Task_Context; // Main per-task variables
TVAR REBSER *Task_Series;	// Series that holds Task_Context

//-- Memory and GC:
TVAR REBPOL *Mem_Pools;		// Memory pool array
TVAR REBINT GC_Disabled;	// GC disabled counter for critical sections.
TVAR REBINT	GC_Ballast;		// Bytes allocated to force automatic GC
TVAR REBOOL	GC_Active;		// TRUE when recycle is enabled (set by RECYCLE func)
TVAR REBSER	*GC_Protect;	// A stack of protected series (removed by pop)
PVAR REBSER	*GC_Mark_Stack; // Series pending to mark their reachables as live
TVAR REBSER	*GC_Series;		// An array of protected series (removed by address)
TVAR REBSER	**GC_Infants;	// A small list of last N series created (nursery)
TVAR REBINT	GC_Last_Infant;	// Index to last infant above (circular)
TVAR REBFLG GC_Stay_Dirty;  // Do not free memory, fill it with 0xBB
TVAR REBSER **Prior_Expand;	// Track prior series expansions (acceleration)

TVAR REBUPT Stack_Limit;	// Limit address for CPU stack.

//-- Evaluation stack:
TVAR REBSER	*DS_Series;
TVAR REBINT	DS_Frame_Index;	// Data stack frame (also index into DS_Base)

TVAR REBOL_STATE *Saved_State; // Saved state for Catch (CPU state, etc.)

//-- Evaluation variables:
TVAR REBI64	Eval_Cycles;	// Total evaluation counter (upward)
TVAR REBI64	Eval_Limit;		// Evaluation limit (set by secure)
TVAR REBINT	Eval_Count;		// Evaluation counter (downward)
TVAR REBINT	Eval_Dose;		// Evaluation counter reset value
TVAR REBCNT	Eval_Sigmask;	// Masking out signal flags

TVAR REBCNT	Trace_Flags;	// Trace flag
TVAR REBINT	Trace_Level;	// Trace depth desired
TVAR REBINT Trace_Depth;	// Tracks trace indentation
TVAR REBCNT Trace_Limit;	// Backtrace buffering limit
TVAR REBSER *Trace_Buffer;	// Holds backtrace lines

TVAR REBI64 Eval_Natives;
TVAR REBI64 Eval_Functions;

//-- Other per thread globals:
TVAR REBSER *Bind_Table;	// Used to quickly bind words to contexts
