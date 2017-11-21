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
#include "pub_tool_addrinfo.h"

/*
************************************************
ENUMS, STRUCTS AND THINGS TO MOVE TO OTHER FILES
************************************************
*/

/* Types of memory accesses */
typedef enum {
	MEM_STORE,			/* Ordinary store */
	MEM_STORE_GUARDED,	/* Guarded store */
	MEM_LOAD,			/* Ordinary load */
	MEM_LOAD_GUARDED	/* Guarded load */
} memory_access_e_t;

/*	Holds a memory access made by a thread */
typedef struct {
	HChar*				file;	/* Which file the memory access is at */
	HChar*				dir;	/* Which directory ^ */
	HChar*				var;	/* Variable name */
	Addr				addr;	/* Which address the memory access points to */
	SizeT				size;	/* Size of memory access, or variable */
	memory_access_e_t	access;	/* Type of memory access, see enum above */
	UInt				line;	/* At which line */
} thread_access_t;

/*	Basic vector for C. */
typedef struct {
	void**	data;					/* Array of pointers to the elements */
	unsigned long long	reserved;	/* Current reserved space to the data array */
	unsigned long long	count;		/* Actually filled spots, count <= reserved */
} vector_t;

static vector_t* new_vector(void)
{
	vector_t*	vector = VG_(malloc)("new_vector_vector", sizeof(vector_t));
	
	vector->reserved = 1;
	vector->count = 0;
	vector->data = VG_(malloc)("new_vector_vector_data", vector->reserved * sizeof(void*));
	
	return vector;
}

static void increase_capacity(vector_t* vector)
{
	void**	new_data;
		
	new_data = VG_(realloc)("increase_capacity_vector_data", vector->data, vector->reserved * 2 * sizeof(void*));
	
	vector->reserved *= 2;
	vector->data = new_data;
}

static void insert_last(vector_t* vector, void* element)
{
	if (vector->count >= vector->reserved)
		increase_capacity(vector);
			
	vector->data[vector->count++] = element;
}

static Bool insert_last_exclusive(vector_t* vector, HChar* element)
{
	unsigned long long	i;
	HChar*				tmp;
		
	for (i = 0; i < vector->count; i++) {
		tmp = vector->data[i];
		
		if (VG_(strcmp)(tmp, element) == 0)
			return False;
	}
	
	insert_last(vector, element);
	
	return True;
}

static void* get(vector_t* vector, unsigned long long index, Bool nested)
{
	vector_t*	intermediate;
	
	if (nested) {
		while (index >= vector->count) {
			intermediate = new_vector();
			insert_last(vector, intermediate);
		}
	}
	
	return vector->data[index];
}

static unsigned long long size(vector_t* vector)
{
	return vector->count;
}

static void free_vector(vector_t* vector)
{
	unsigned long long	i;
	unsigned long long	n;
	
	n = vector->count;
	
	for (i = 0; i < n; i++)
		VG_(free)(vector->data[i]);
		
	VG_(free)(vector->data);
	VG_(free)(vector);
}

/*
****************
GLOBAL VARIABLES
****************
*/

static Bool			fg_allow_empty_filename = True;

//	TODO: Replace this with a e.g hash table
static vector_t*	g_accesses;
static vector_t*	g_collisions;
static HChar*		g_filename;
static HChar*		g_dirname;
static UInt			g_cache_alignment = 64;	/*	CHANGE THIS LATER */

/*
********************
FALSEGRIND FUNCTIONS
********************
*/

static Bool access_in_range(thread_access_t* first, thread_access_t* second)
{
	long long difference;
	
	if (first->addr == second->addr)
		return False;	/* Ignore possible data race, not our job */
		
	if (first->access >= MEM_LOAD && first->access <= MEM_LOAD_GUARDED)
		if (second->access >= MEM_LOAD && second->access <= MEM_LOAD_GUARDED)
			return False;	/* Ignore two loads */
		
	difference = first->addr - second->addr;
	
	if (difference < 0)
		difference *= (-1);
			
	if (difference >= g_cache_alignment)
		return False;	/* Does not fit into same cache block */
	
	if (VG_(strcmp)(first->file, second->file) != 0)
		return False;	/* Not even same file */
		
	return True;
}

UInt int_length(UInt nbr)
{
	UInt	n;
	
	for (n = 0; nbr > 0; nbr /= 10)
		n++;
		
	return n;
}

HChar* int_to_char_array(UInt nbr)
{
	HChar*	result;
	UInt	i;
	UInt	n;
	
	n = int_length(nbr);
	result = VG_(malloc)("", sizeof(HChar) * n);
	
	for (i = 0; i < n; i++) {
		result[i] = nbr % 10;
		nbr /= 10;
	}
	
	return result;
}

/*	This is probably slow, opt for hash table later */
static void check_false_sharing(thread_access_t* access, ThreadId tid)
{
	thread_access_t*	tmp;
	vector_t*			tmp_vector;
	unsigned long long	collisions;
	//vector_t*			lines;
	ThreadId			i;
	unsigned long long	j;
	ThreadId			thread_count;
	unsigned long long	element_count;
	HChar*				tmp_collision_info;
	
	UInt				file_len;
	UInt				var_len;
	UInt				a_line_len;
	UInt				t_line_len;
	
	thread_count = size(g_accesses);
	collisions = 0;
	//lines = new_vector();
	
	/* Thread 0 does not exist, starts from 1 */
	for (i = 1; i < thread_count; i++) {
		if (i == tid)	// Can't get collisions with same thread
			continue;
			
		tmp_vector = get(g_accesses, i, True);
		element_count = size(tmp_vector);
		
		for (j = 0; j < element_count; j++) {
			tmp = get(tmp_vector, j, False);
						
			if (access_in_range(access, tmp)) {
				collisions++;
				
				file_len = VG_(strlen)(access->file);
				var_len = VG_(strlen)(access->var);
				a_line_len = int_length(access->line);
				t_line_len = int_length(tmp->line);
				
				tmp_collision_info = VG_(malloc)("", sizeof(HChar) * (file_len + var_len + a_line_len + t_line_len + 1));
				VG_(memcpy)(tmp_collision_info, access->file, file_len);
				VG_(memcpy)(tmp_collision_info + file_len, access->var, var_len);
				VG_(memcpy)(tmp_collision_info + file_len + var_len, int_to_char_array(access->line), a_line_len);
				VG_(memcpy)(tmp_collision_info + file_len + var_len + a_line_len, int_to_char_array(tmp->line), t_line_len);
				tmp_collision_info[file_len + var_len + a_line_len + t_line_len] = '\0';
				
				if (insert_last_exclusive(g_collisions, tmp_collision_info))
					VG_(printf)("false sharing possible in \"%s\" in variable \"%s\" with lines %d vs %d\n", access->file, access->var, access->line, tmp->line);
				else
					VG_(free)(tmp_collision_info);
				
				/*
				tmp_line = VG_(malloc)("check_false_sharing", sizeof(UInt));
				*tmp_line = tmp->line;

				//VG_(printf)("false sharing possible in %s:%d vs %s:%d\n", access->file, access->line, tmp->file, tmp->line);
				
				insert_last(lines, tmp_line);
				*/
			}
		}
	}
	
	/*
	if (collisions > 0) {
		for (i = 0; i < size(lines); i++)
			VG_(printf)("false sharing might occur in line %d vs line %d\n", access->line, *(UInt*)get(lines, i, False));
	}
		
	free_vector(lines);
	*/
}

static VG_REGPARM(2) void add_to_vector(thread_access_t* access, Addr a)
{
	ThreadId	tid;
	AddrInfo 	ai;
	
	tid = VG_(get_running_tid)();
	VG_(describe_addr)(a, &ai);
	
	if (ai.tag != Addr_DataSym)
		return;
		
	/*
	if (tid != addr->tid)
		VG_(printf)("***** NOT SAME TID *****\ntid: %d vs addr->tid: %d\n***** NOT SAME TID *****\n", tid, addr->tid);
	*/
	
	access->addr = a;
	access->var = VG_(malloc)("add_to_vector_access->var", sizeof(HChar) * (VG_(strlen)(ai.Addr.DataSym.name) + 1));
	VG_(strcpy)(access->var, ai.Addr.DataSym.name);
	
	#if 0
	VG_(printf)("a is %lx, var name: %s, thread: %d\n", access->addr, ai.Addr.DataSym.name, tid);
	
	if (access->access == MEM_STORE || access->access == MEM_STORE_GUARDED)
		VG_(printf)("and it was a store\n");
	else
		VG_(printf)("and it was a load\n");
	#endif
		
	insert_last(get(g_accesses, tid, True), access);
	check_false_sharing(access, tid);
}

static void add_access(memory_access_e_t access, IRSB* sb, IRType type /* also includes size */, IRExpr* guard, ThreadId tid, IRExpr* addr, HChar* file, UInt line)
{
	IRDirty*			di;
	IRExpr**			argv;
	thread_access_t*	t_a;
	
	/*
	//	TODO: Change this to general case obviously
	if (VG_(strcmp)(file, "false_sharing_valgrind.c") != 0)
		return;
	*/
	
	t_a = VG_(malloc)("add_access_t_a", sizeof(thread_access_t));
		
	t_a->addr = addr;
	t_a->access = access;
	t_a->file = NULL;
	t_a->dir = NULL;
	t_a->line = line;
	
	t_a->file = VG_(malloc)("add_access_t_a_file", sizeof(HChar) * (VG_(strlen)(g_filename) + 1));
	VG_(strcpy)(t_a->file, g_filename);
	
	if (type == Ity_I32)
		t_a->size = 4;
	else if (type == Ity_I64)
		t_a->size = 8;
	else
		t_a->size = 0;
	
	argv = mkIRExprVec_2(mkIRExpr_HWord((HWord)t_a), addr);
	di = unsafeIRDirty_0_N(1, "add_to_vector", VG_(fnptr_to_fnentry)(&add_to_vector), argv);
	
	if (guard)
		di->guard = guard;
		
	addStmtToIRSB(sb, IRStmt_Dirty(di));
}

/*
****************************
VALGRIND SPECIFIED FUNCTIONS
****************************
*/

static void fg_post_clo_init(void) {}

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
		
	if (!VG_(get_filename_linenum)(closure->nraddr, &g_filename, &g_dirname, &line)) {
		//g_filename = NULL;
		//g_dirname = NULL;
		line = 0;
	}
		
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
	/*
	Int	i;
	Int	j;
	
	for (i = 0; i < 3; i++) {
		for (j = 0; j < accesses[i].count; j++) {
			check_collisions(i + 1, get(&accesses[i], j));
		}
	}
	*/
	
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
	
	g_accesses = new_vector();
	g_collisions = new_vector();
}

VG_DETERMINE_INTERFACE_VERSION(fg_pre_clo_init)