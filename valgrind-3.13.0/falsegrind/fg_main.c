
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

// START

static void fg_track_new_mem_stack_signal(Addr a, SizeT len, ThreadId tid)
{
	VG_(printf)("here\n");
	//VG_(exit)(1);
}

static void fg_track_new_mem_brk(Addr a, SizeT len, ThreadId tid)
{
	VG_(printf)("brk\n");
	VG_(printf)("alloc %zu for thread %u\n", len, tid);
}

static void fg_track_new_mem_stack(Addr a, SizeT len)
{
	AddrInfo	ai = { .tag = Addr_Undescribed };
	VG_(describe_addr)(a, &ai);
	//VG_(pp_addrinfo)(a, &ai);
	
	if (len > 2000) {
		VG_(pp_addrinfo)(a, &ai);
		VG_(printf)("found big alloc for %zu\n", len);
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

// BASIC TOOLS

static void fg_post_clo_init(void)
{
}

static
IRSB* fg_instrument ( VgCallbackClosure* closure,
                      IRSB* bb,
                      const VexGuestLayout* layout, 
                      const VexGuestExtents* vge,
                      const VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
    return bb;
}

static void fg_fini(Int exitcode)
{
	VG_(printf)("TJENA 8\n");
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
