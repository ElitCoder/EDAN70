
/*--------------------------------------------------------------------*/
/*--- Nulgrind: The minimal Valgrind tool.               fg_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Nulgrind, the minimal Valgrind tool,
   which does no instrumentation or analysis.

   Copyright (C) 2002-2017 Nicholas Nethercote
      njn@valgrind.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_basics.h"
#include "pub_tool_xarray.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_addrinfo.h"
#include "pub_tool_machine.h" 

static Bool clo_detailed_counts = True;

typedef enum { OpLoad=0, OpStore=1, OpAlu=2 } Op;

typedef
   IRExpr 
   IRAtom;
   
   #define N_OPS 3


   /* --- Types --- */

   #define N_TYPES 14
   
static ULong detailCounts[N_OPS][N_TYPES];
   
static Int type2index ( IRType ty )
 {
    switch (ty) {
       case Ity_I1:      return 0;
       case Ity_I8:      return 1;
       case Ity_I16:     return 2;
       case Ity_I32:     return 3;
       case Ity_I64:     return 4;
       case Ity_I128:    return 5;
       case Ity_F32:     return 6;
       case Ity_F64:     return 7;
       case Ity_F128:    return 8;
       case Ity_V128:    return 9;
       case Ity_V256:    return 10;
       case Ity_D32:     return 11;
       case Ity_D64:     return 12;
       case Ity_D128:    return 13;
       default: tl_assert(0);
    }
 }
 
 static const HChar* nameOfTypeIndex ( Int i )
{
   switch (i) {
      case 0: return "I1";   break;
      case 1: return "I8";   break;
      case 2: return "I16";  break;
      case 3: return "I32";  break;
      case 4: return "I64";  break;
      case 5: return "I128"; break;
      case 6: return "F32";  break;
      case 7: return "F64";  break;
      case 8: return "F128";  break;
      case 9: return "V128";  break;
      case 10: return "V256"; break;
      case 11: return "D32";  break;
      case 12: return "D64";  break;
      case 13: return "D128"; break;
      default: tl_assert(0);
   }
}
 
 static void print_details ( void )
{
   Int typeIx;
   VG_(umsg)("   Type        Loads       Stores       AluOps\n");
   VG_(umsg)("   -------------------------------------------\n");
   for (typeIx = 0; typeIx < N_TYPES; typeIx++) {
      VG_(umsg)("   %-4s %'12llu %'12llu %'12llu\n",
                nameOfTypeIndex( typeIx ),
                detailCounts[OpLoad ][typeIx],
                detailCounts[OpStore][typeIx],
                detailCounts[OpAlu  ][typeIx]
      );
   }
}
 
 void increment_detail(ULong* detail)
 {
    (*detail)++;
 }

static void instrument_detail(IRSB* sb, Op op, IRType type, IRAtom* guard)
{
   IRDirty* di;
   IRExpr** argv;
   const UInt typeIx = type2index(type);

   tl_assert(op < N_OPS);
   tl_assert(typeIx < N_TYPES);

   argv = mkIRExprVec_1( mkIRExpr_HWord( (HWord)&detailCounts[op][typeIx] ) );
   di = unsafeIRDirty_0_N( 1, "increment_detail",
                              VG_(fnptr_to_fnentry)( &increment_detail ), 
                              argv);
   if (guard) di->guard = guard;
   addStmtToIRSB( sb, IRStmt_Dirty(di) );
}

// START

static void fg_track_new_mem_stack_signal(Addr a, SizeT len, ThreadId tid)
{
	//VG_(printf)("here\n");
	//VG_(exit)(1);
}

static void fg_track_new_mem_brk(Addr a, SizeT len, ThreadId tid)
{
	//VG_(printf)("brk\n");
	//VG_(printf)("alloc %zu for thread %u\n", len, tid);
}

static void fg_track_new_mem_stack(Addr a, SizeT len)
{
	AddrInfo	ai = { .tag = Addr_Undescribed };
	VG_(describe_addr)(a, &ai);
	//VG_(pp_addrinfo)(a, &ai);
	
	if (len > 2000) {
		//VG_(pp_addrinfo)(a, &ai);
		//VG_(printf)("found big alloc for %zu\n", len);
	}
	
	//VG_(printf)("stack %zu\n", len);
}

/*
static void fg_track_new_mem_stack_w_ECU(Addr a, SizeT len, UInt ecu)
{
	VG_(printf)("stack_w_ECU %zu\n", len);
}
*/

static void fg_track_die_mem_stack(Addr a, SizeT len)
{
	//VG_(printf)("die_stack %zu\n", len);
}

static void fg_evaluate_IRExpr(IRExpr* e)
{
	
}

// BASIC TOOLS

static void fg_post_clo_init(void)
{
}

static int					stmts[10];
static unsigned long long	calls;

static
IRSB* fg_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn, 
                      const VexGuestLayout* layout, 
                      const VexGuestExtents* vge,
                      const VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
   IRDirty*   di;
   Int        i;
   IRSB*      sbOut;
   IRTypeEnv* tyenv = sbIn->tyenv;
   Addr       iaddr = 0, dst;
   UInt       ilen = 0;
   Bool       condition_inverted = False;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   /* Set up SB */
   sbOut = deepCopyIRSBExceptStmts(sbIn);

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( sbOut, sbIn->stmts[i] );
      i++;
   }

   for (/*use current i*/; i < sbIn->stmts_used; i++) {
      IRStmt* st = sbIn->stmts[i];
      if (!st || st->tag == Ist_NoOp) continue;
      
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_IMark:
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_WrTmp:
            // Add a call to trace_load() if --trace-mem=yes.
			
            if (clo_detailed_counts) {
               IRExpr* expr = st->Ist.WrTmp.data;
               IRType  type = typeOfIRExpr(sbOut->tyenv, expr);
               tl_assert(type != Ity_INVALID);
               switch (expr->tag) {
                  case Iex_Load:
                    instrument_detail( sbOut, OpLoad, type, NULL/*guard*/ );
                     break;
                  case Iex_Unop:
                  case Iex_Binop:
                  case Iex_Triop:
                  case Iex_Qop:
                  case Iex_ITE:
                     instrument_detail( sbOut, OpAlu, type, NULL/*guard*/ );
                     break;
                  default:
                     break;
               }
            }
			
            addStmtToIRSB( sbOut, st );
            break;

         case Ist_Store: {
            IRExpr* data = st->Ist.Store.data;
            IRType  type = typeOfIRExpr(tyenv, data);
            tl_assert(type != Ity_INVALID);
			stmts[0]++;
            if (clo_detailed_counts) {
               instrument_detail( sbOut, OpStore, type, NULL/*guard*/ );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_StoreG: {
            IRStoreG* sg   = st->Ist.StoreG.details;
            IRExpr*   data = sg->data;
            IRType    type = typeOfIRExpr(tyenv, data);
            tl_assert(type != Ity_INVALID);
			stmts[1]++;
            if (clo_detailed_counts) {
               instrument_detail( sbOut, OpStore, type, sg->guard );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_LoadG: {
            IRLoadG* lg       = st->Ist.LoadG.details;
            IRType   type     = Ity_INVALID; /* loaded type */
            IRType   typeWide = Ity_INVALID; /* after implicit widening */
            typeOfIRLoadGOp(lg->cvt, &typeWide, &type);
            tl_assert(type != Ity_INVALID);
            if (clo_detailed_counts) {
               instrument_detail( sbOut, OpLoad, type, lg->guard );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_Dirty: {
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_CAS: {
            /* We treat it as a read and a write of the location.  I
               think that is the same behaviour as it was before IRCAS
               was introduced, since prior to that point, the Vex
               front ends would translate a lock-prefixed instruction
               into a (normal) read followed by a (normal) write. */
            Int    dataSize;
            IRType dataTy;
            IRCAS* cas = st->Ist.CAS.details;
            tl_assert(cas->addr != NULL);
            tl_assert(cas->dataLo != NULL);
            dataTy   = typeOfIRExpr(tyenv, cas->dataLo);
            dataSize = sizeofIRType(dataTy);
			stmts[2]++;
			stmts[3]++;
            if (cas->dataHi != NULL)
               dataSize *= 2; /* since it's a doubleword-CAS */
            if (clo_detailed_counts) {
               instrument_detail( sbOut, OpLoad, dataTy, NULL/*guard*/ );
               if (cas->dataHi != NULL) /* dcas */
                  instrument_detail( sbOut, OpLoad, dataTy, NULL/*guard*/ );
               instrument_detail( sbOut, OpStore, dataTy, NULL/*guard*/ );
               if (cas->dataHi != NULL) /* dcas */
                  instrument_detail( sbOut, OpStore, dataTy, NULL/*guard*/ );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_LLSC: {
            IRType dataTy;
            if (st->Ist.LLSC.storedata == NULL) {
               /* LL */
               dataTy = typeOfIRTemp(tyenv, st->Ist.LLSC.result);
               if (clo_detailed_counts)
                  instrument_detail( sbOut, OpLoad, dataTy, NULL/*guard*/ );
            } else {
               /* SC */
               dataTy = typeOfIRExpr(tyenv, st->Ist.LLSC.storedata);
			   stmts[4]++;
               if (clo_detailed_counts)
                  instrument_detail( sbOut, OpStore, dataTy, NULL/*guard*/ );
            }
            addStmtToIRSB( sbOut, st );
            break;
         }

         case Ist_Exit:

            addStmtToIRSB( sbOut, st );      // Original statement
            break;

         default:
            ppIRStmt(st);
            tl_assert(0);
      }
   }

   return sbOut/*sbOut*/;
}

static
IRSB* fg_instrument2 ( VgCallbackClosure* closure,
                      IRSB* bb,
                      const VexGuestLayout* layout, 
                      const VexGuestExtents* vge,
                      const VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
	IRSB*		bo;
	IRStmt*		st;
	IRExpr*		data;
	IRExpr*		addr;
	IRType		type;
	IRStoreG*	sg;
	IRCAS*		cas;
	Int			i;
	
	bo = deepCopyIRSBExceptStmts(bb);

	i = 0;
	while (i < bb->stmts_used && bb->stmts[i]->tag != Ist_IMark) {
		addStmtToIRSB(bo, bb->stmts[i]);
		
		i++;
	}
	
	/*
	if (closure->tid > call_max)
		call_max = closure->tid;
		
	calls[closure->tid]++;
	*/
	
	//VG_(printf)("called by %d\n", closure->tid);
	
	int loops = 0;
	
	for (; i < bb->stmts_used; i++) {
		st = bb->stmts[i];
		
		loops++;
		
		if (loops > 1) {
			//VG_(printf)("actually loops\n");
			//VG_(exit)(1);
		}
		
		if (!st || st->tag == Ist_NoOp)
			continue;
			
		calls++;
			
		switch (st->tag) {
			case Ist_StoreG: {
				sg = st->Ist.StoreG.details;
				data = sg->data;
				type = typeOfIRExpr(bb->tyenv, data);
				
				stmts[0]++;
				
				break;
			}
			
			case Ist_CAS: {
				cas = st->Ist.CAS.details;
				
				stmts[2]++;
				
				if (cas->dataHi != NULL)
					stmts[3]++;
				
				break;
			}
			
			case Ist_LLSC: {
				if (st->Ist.LLSC.storedata != NULL)
					stmts[4]++;
					
				break;
			}
			
			case Ist_Store: {
	            data = st->Ist.Store.data;
				addr = st->Ist.Store.addr;
	           	type = typeOfIRExpr(bb->tyenv, data);
				
				if (type == Ity_INVALID)
					VG_(exit)(1);
					
				stmts[1]++;
				
				/*
					Structure:
					IRSB:
						IRTypeEnv:
							IRType:
								(type of value, Ity_I64 etc.)
						IRStmt:
							(what statement, union)
							IRStmtTag:
								(type of statement to pick from union, Ist_Store etc.)
							IRExpr:
								(what expression, union)
								IRExprTag:
									(type of tag, Iex_Const etc.)
								IRConst:
									(what const value, union)
									IRConstTag:
										(type of tag, Ico_U32 etc.)
				*/
				
				//VG_(printf)("tag is %04X for %d\n", addr->tag, type);
				
				/*
				if (addr->tag == Iex_Const) {
					VG_(printf)("const is %04X %d\n", addr->Iex.Const.con->tag, addr->Iex.Const.con->tag);
					
					if (addr->Iex.Const.con->tag == Ico_U64)
						VG_(printf)("with value %08X\n", addr->Iex.Const.con->Ico.U64);
				}
				*/
					
				
				
				/*
	            tl_assert(type != Ity_INVALID);
	            if (clo_trace_mem) {
	               addEvent_Dw( sbOut, st->Ist.Store.addr,
	                            sizeofIRType(type) );
	            }
	            if (clo_detailed_counts) {
	               instrument_detail( sbOut, OpStore, type, NULL );
	            }
	            addStmtToIRSB( sbOut, st );
				*/
				//VG_(printf)("found a store\n");
	            break;
	         }
			 
		
		}
	}
	
    return bb;
}

static void fg_fini(Int exitcode)
{
	int	i;
	
	VG_(printf)("stores:\n");
	VG_(printf)("StoreG: %d\n", stmts[0]);
	VG_(printf)("Store: %d\n", stmts[1]);
	VG_(printf)("CAS-L: %d\n", stmts[2]);
	VG_(printf)("CAS-R: %d\n", stmts[3]);
	VG_(printf)("LLSC: %d\n", stmts[4]);
	VG_(printf)("CALLS: %llu\n", calls);
	
	VG_(printf)("From LACKEY:\n");
	print_details();
	
	VG_(printf)("Compiled %s at %s\n", __DATE__, __TIME__);
}

static void fg_pre_clo_init(void)
{
   VG_(details_name)            ("Falsegrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("a tool for detecting false sharing in multiprocessors");
   VG_(details_copyright_author)(
      "Copyright (C) 2017, and GNU GPL'd, by Niklas Carlsson.");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);

   VG_(details_avg_translation_sizeB) ( 275 );

   VG_(basic_tool_funcs)        	(fg_post_clo_init,
                                 	 fg_instrument,
                                 	 fg_fini);
								 
	VG_(track_new_mem_stack_signal)	(fg_track_new_mem_stack_signal);
	VG_(track_new_mem_brk)			(fg_track_new_mem_brk);
	VG_(track_new_mem_stack)		(fg_track_new_mem_stack);
	//VG_(track_new_mem_stack_w_ECU)	(fg_track_new_mem_stack_w_ECU);
	VG_(track_die_mem_stack)		(fg_track_die_mem_stack);
	//VG_(needs_var_info)				();
	
	//VG_(exit)(1);
	VG_(printf)("HEJJEJJE\n");
	//VG:
	//VG:

   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(fg_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
