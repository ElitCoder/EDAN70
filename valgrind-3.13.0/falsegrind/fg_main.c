#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_options.h"
#include "pub_tool_machine.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_threadstate.h"

typedef enum {
	MEM_STORE,
	MEM_LOAD,
	MEM_STORE_GUARDED,
	MEM_LOAD_GUARDED
} memory_access_e_t;

/*	Strucure needs:
	THREAD ID
	ADDRESS
	SIZE OF ACCESS 
	LINE OF CODE 
	FILENAME?
	DIRNAME?
*/
typedef struct {
	HChar*				file;
	HChar*				dir;
	IRExpr*				addr;
	SizeT				size;
	memory_access_e_t	access;
	UInt				line;
	IRType				type;
//	ThreadId			tid;
} thread_access_t;

/*	Basic vector for C. */
typedef struct {
	thread_access_t**	data;
	unsigned long long	reserved;
	unsigned long long	count;
} vector_t;

static void init_vector(vector_t* vector)
{
	vector->count = 0;
	vector->reserved = 1;
	
	vector->data = VG_(malloc)("init_vector_vector_data", vector->reserved * sizeof(thread_access_t*));
}
static void increase_capacity(vector_t* vector)
{
	thread_access_t**	new_data;
		
	new_data = VG_(realloc)("vector->data", vector->data, vector->reserved * 2 * sizeof(thread_access_t*));
	
	vector->reserved *= 2;
	vector->data = new_data;
}

static void insert_last(vector_t* vector, thread_access_t* access)
{
	if (vector->count >= vector->reserved)
		increase_capacity(vector);
			
	vector->data[vector->count++] = access;
}

static thread_access_t* get(vector_t* vector, unsigned long long index)
{
	return vector->data[index];
}

static unsigned long long size(vector_t* vector)
{
	return vector->count;
}

static HChar*	g_filename;
static HChar*	g_dirname;

static vector_t	accesses[3 /* Change this to nbr of threads */];

static Int check_collisions(ThreadId tid, thread_access_t* access)
{
	vector_t*			vector;
	thread_access_t*	current;
	unsigned long long 	count;
	unsigned long long 	j;
	Int					nbr_threads;
	Int					ignore;
	Int					i;
	
	nbr_threads = 3;
	ignore = tid - 1;
	
	for (i = 0; i < nbr_threads; i++) {
		if (i == ignore)
			continue;
			
		vector = &accesses[i];
		count = size(vector);
		
		for (j = 0; j < count; j++) {
			current = get(vector, j);
			
			if (current->access != MEM_STORE)
				continue;
			
			long long a = (long long)access->addr;
			long long b = (long long)current->addr;
			
			VG_(printf)("distance: %d %d %d %d %d\n", a - b, a, b, current->line, access->line);
			
			if (current->addr == access->addr) {
				
					
				
					
				VG_(printf)("found match t %d %d %04X %d %d\n", i + 1, tid, access->addr, current->line, access->line);
				
				if (access->addr->tag == Iex_Unop) {
					VG_(printf)("%d\n", access->addr->Iex.Unop.arg->Iex.Const.con->Ico.U32);
				}
				
				return 1;
			}
		}
	}
	
	return 0;
	
	/*
	Int					ignore;
	thread_access_t*	current;
	unsigned long long	i;
	unsigned long long 	count;
	
	count = size(vector);
	ignore = tid - 1;
	
	for (i = 0; i < count; i++) {
		current = get(vector, i);
		
		if (current->addr == access->addr)
			VG_(printf)("holy nigger same address\n");
	}
	
	return 0;
	*/
}

static VG_REGPARM(1) void add_to_vector(thread_access_t* addr)
{
	
	ThreadId	tid;
	
	tid = VG_(get_running_tid)();
	//tid = addr->tid;
	
	/*
	if (check_collisions(tid, addr))
		VG_(printf)("FALSE SHARING IS HAPPENING line: %d\n", addr->line);
		*/
		
	insert_last(&accesses[tid - 1], addr);
	//VG_(printf)("actually running this %zu from thread %d\n", accesses[0].count, addr->tid);
}

static void add_access(memory_access_e_t access, IRSB* sb, IRType type /* also includes size */, IRExpr* guard, ThreadId tid, IRExpr* addr, HChar* file, UInt line)
{
	IRDirty*			di;
	IRExpr**			argv;
	thread_access_t*	t_a;
	
	if (VG_(strcmp)(file, "false_sharing_valgrind.c") != 0)
		return;
	
	t_a = VG_(malloc)("add_access_t_a", sizeof(thread_access_t));
	//VG_(printf)("type: %04X\n", type);
	
	tid = VG_(get_running_tid)();
	
	t_a->addr = addr;
	t_a->access = access;
	t_a->file = NULL;
	t_a->dir = NULL;
	t_a->size = 4 /* Change this obviously */;
	t_a->line = line /* This too */;
	t_a->type = type;
	//t_a->tid = tid;
		
	argv = mkIRExprVec_1(mkIRExpr_HWord((HWord)t_a));
	di = unsafeIRDirty_0_N(1, "add_to_vector", VG_(fnptr_to_fnentry)(&add_to_vector), argv);
	
	if (guard)
		di->guard = guard;
		
	addStmtToIRSB(sb, IRStmt_Dirty(di));
}

static void fg_post_clo_init(void)
{
}

static IRSB* fg_instrument(VgCallbackClosure* closure, IRSB* sbIn, const VexGuestLayout* layout, const VexGuestExtents* vge, const VexArchInfo* archinfo, IRType gWordTy, IRType hWordTy)
{
	IRSB*		sbOut;
	IRStmt*		st;
	IRExpr*		expr;
	IRType		type;
	IRType		type_wide;
	IRStoreG*	sg;
	IRLoadG*	lg;
	IRCAS*		cas;
	UInt		line;
	Int			i;
	
	if (gWordTy != hWordTy)
		VG_(tool_panic)("host/guest word size does not match");
		
	if (!VG_(get_filename_linenum)(closure->nraddr, &g_filename, &g_dirname, &line))
		line = 0;
		
		
	if (line == 21)
		VG_(printf)("FOUND\n");
		
	sbOut = deepCopyIRSBExceptStmts(sbIn);
	
	i = 0;
	while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
		addStmtToIRSB(sbOut, sbIn->stmts[i]);
		i++;
	}
	
	for (; i < sbIn->stmts_used; i++) {
		st = sbIn->stmts[i];
		
		if (!st || st->tag == Ist_NoOp)
			continue;
			
		switch (st->tag) {
			case Ist_NoOp:
			case Ist_AbiHint:
			case Ist_Put:
			case Ist_PutI:
			case Ist_MBE:
			case Ist_IMark:
			case Ist_Dirty:
			case Ist_Exit:
				addStmtToIRSB(sbOut, st);
				break;
				
			case Ist_WrTmp: {
				expr = st->Ist.WrTmp.data;
				type = typeOfIRExpr(sbOut->tyenv, expr);
				
				if (expr->tag == Iex_Load)	
					add_access(MEM_LOAD, sbOut, type, NULL, closure->tid, st->Ist.WrTmp.data->Iex.Load.addr, g_filename, line);
				
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_Store: {
				type = typeOfIRExpr(sbIn->tyenv, st->Ist.Store.addr);
			
				add_access(MEM_STORE, sbOut, type, NULL, closure->tid, st->Ist.Store.addr, g_filename, line);
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_StoreG: {
				sg = st->Ist.StoreG.details;
				expr = sg->data;
				type = typeOfIRExpr(sbIn->tyenv, expr);
				
				add_access(MEM_STORE_GUARDED, sbOut, type, sg->guard, closure->tid, sg->addr, g_filename, line);
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_LoadG: {
				lg = st->Ist.LoadG.details;
				type = Ity_INVALID;
				type_wide = Ity_INVALID;
				
				typeOfIRLoadGOp(lg->cvt, &type_wide, &type);
				add_access(MEM_LOAD_GUARDED, sbOut, type, lg->guard, closure->tid, lg->addr, g_filename, line);
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_CAS: {
				cas = st->Ist.CAS.details;
				type = typeOfIRExpr(sbIn->tyenv, cas->dataLo);
				
				add_access(MEM_LOAD, sbOut, type, NULL, closure->tid, cas->addr, g_filename, line);
				add_access(MEM_STORE, sbOut, type, NULL, closure->tid, cas->addr, g_filename, line);
				
				if (cas->dataHi != NULL) {
					add_access(MEM_LOAD, sbOut, type, NULL, closure->tid, cas->addr, g_filename, line);
					add_access(MEM_STORE, sbOut, type, NULL, closure->tid, cas->addr, g_filename, line);
				}
				
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_LLSC: {
				if (st->Ist.LLSC.storedata == NULL) {
					type = typeOfIRTemp(sbIn->tyenv, st->Ist.LLSC.result);
					add_access(MEM_LOAD, sbOut, type, NULL, closure->tid, st->Ist.LLSC.addr, g_filename, line);
				} else {
					type = typeOfIRExpr(sbIn->tyenv, st->Ist.LLSC.storedata);
					add_access(MEM_STORE, sbOut, type, NULL, closure->tid, st->Ist.LLSC.addr, g_filename, line);
				}
				
				addStmtToIRSB(sbOut, st);
				break;
			}
		}
	}
	
	return sbOut;
}

static void fg_fini(Int exitcode)
{	
	Int	i;
	Int	j;
	
	for (i = 0; i < 3; i++) {
		for (j = 0; j < accesses[i].count; j++) {
			check_collisions(i + 1, get(&accesses[i], j));
		}
	}
	
	VG_(printf)("Compiled %s at %s\n", __DATE__, __TIME__);
}

static void fg_pre_clo_init(void)
{
	VG_(details_name)					("Falsegrind");
	VG_(details_version)				(NULL);
	VG_(details_description)			("a tool for detecting false sharing in multiprocessors");
	VG_(details_copyright_author)		("by Niklas Carlsson");
	VG_(details_bug_reports_to)			(VG_BUGS_TO);
	VG_(details_avg_translation_sizeB)	(275);
	
	VG_(basic_tool_funcs)				(fg_post_clo_init, fg_instrument, fg_fini);
	
	g_filename = VG_(malloc)("g_filename", sizeof(HChar) * 255);
	g_dirname = VG_(malloc)("g_dirname", sizeof(HChar) * 255);
	
	init_vector(&accesses[0]);
	init_vector(&accesses[1]);
	init_vector(&accesses[2]);
}

VG_DETERMINE_INTERFACE_VERSION(fg_pre_clo_init)