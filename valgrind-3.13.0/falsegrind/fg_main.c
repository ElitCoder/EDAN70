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

#define FG_ERROR(...)	do { VG_(printf)("ERROR (%s:%d): ", __FUNCTION__, __LINE__); VG_(printf)(__VA_ARGS__); VG_(printf)("\n"); VG_(exit)(1); } while (0);

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
	Addr				addr;		/* Which address the memory access points to */
	memory_access_e_t	access;		/* Type of memory access, see enum above */
	UInt				line;		/* At which line */
	Int					file_nr;	/* Which file the memory access is in, refers to g_filenames */
	Int					dir_nr;		/* Same as above, but for directories */
} thread_access_t;

typedef struct {
	thread_access_t*	first;
	thread_access_t*	second;
	unsigned long long	collisions;
	ThreadId			fst_tid;
	ThreadId			snd_tid;
	Bool				is_true_sharing;
} collision_t;

/*	Basic vector for C */
typedef struct {
	void**	data;					/* Array of pointers to the elements */
	unsigned long long	reserved;	/* Current reserved space to the data array */
	unsigned long long	count;		/* Actually filled spots, count <= reserved */
} vector_t;

static Bool collision_equal(collision_t* first, collision_t* second)
{
	if (first->first->line != second->first->line)
		return False;
		
	if (first->second->line != second->second->line)
		return False;
		
	if (first->first->file_nr != second->first->file_nr)
		return False;
		
	if (first->fst_tid != second->fst_tid)
		return False;
		
	if (first->snd_tid != second->snd_tid)
		return False;
		
	return True;
}

static Bool collision_equal_final(collision_t* first, collision_t* second)
{
	Bool	line = False;
	
	if (first->first->line == second->first->line && first->second->line == second->second->line)
		line = True;
	else if (first->first->line == second->second->line && first->second->line == second->first->line)
		line = True;
		
	return line;
}

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

static void check_capacity(vector_t* vector)
{
	if (vector->count >= vector->reserved)
		increase_capacity(vector);
}

static void insert_last(vector_t* vector, void* element)
{
	check_capacity(vector);
	
	vector->data[vector->count++] = element;
}

static void insert_last_malloc(vector_t* vector, HChar* element)
{
	HChar*	name = VG_(malloc)("", sizeof(HChar) * (VG_(strlen)(element) + 1));
	VG_(strcpy)(name, element);
	name[VG_(strlen)(element)] = '\0';
	
	insert_last(vector, element);
}

static Bool contains(vector_t* vector, HChar* element)
{
	unsigned long long	i;
		
	for (i = 0; i < vector->count; i++)
		if (VG_(strcmp)(vector->data[i], element) == 0)
			return True;
			
	return False;
}

static Bool contains_collision(vector_t* vector, collision_t* collision)
{
	unsigned long long	i;
	collision_t*		current;
		
	for (i = 0; i < vector->count; i++) {
		current = vector->data[i];
		
		if (collision_equal(collision, current)) {
			current->collisions++;
			
			return True;
		}
		
		/*	
		if (current->file_nr == collision->file_nr && current->line1 == collision->line1 && current->line2 == collision->line2)
			return True;
			*/
	}
			
	return False;
}

static Int contains_get_index(vector_t* vector, HChar* element)
{
	unsigned long long	i;
		
	for (i = 0; i < vector->count; i++)
		if (VG_(strcmp)(vector->data[i], element) == 0)
			return i;
			
	// Was not found		
	return -1;
}

static Bool insert_last_exclusive_access(vector_t* vector, thread_access_t* access)
{
	unsigned long long	i;
	
	for (i = 0; i < vector->count; i++)
		if (access == vector->data[i])
			return False;
			
	insert_last(vector, access);
	
	return True;		
}

static Bool insert_last_exclusive(vector_t* vector, HChar* element)
{
	if (contains(vector, element))
		return False;
	
	insert_last(vector, element);
	
	return True;
}

/* TODO: Make this generic if there's time */
static Bool insert_last_exclusive_malloc(vector_t* vector, HChar *element)
{
	HChar*	insert;
	
	if (contains(vector, element))
		return False;
		
	insert = VG_(malloc)("insert_last_exclusive_malloc_insert", sizeof(HChar) * (VG_(strlen)(element) + 1));
	insert_last(vector, insert);
	
	return True;
}

static Bool insert_last_exclusive_collision_final(vector_t* vector, collision_t* collision)
{
	unsigned long long	i;
	
	for (i = 0; i < vector->count; i++)
		if (collision_equal_final(vector->data[i], collision))
			return False;
			
	insert_last(vector, collision);
	
	return True;
}

static Bool insert_last_exclusive_malloc_collision(vector_t* vector, collision_t* collision)
{
	collision_t*	col;
	
	if (contains_collision(vector, collision))
		return False;
	
	col = VG_(malloc)("", sizeof(collision_t));
	col->first = collision->first;
	col->second = collision->second;
	col->fst_tid = collision->fst_tid;
	col->snd_tid = collision->snd_tid;
	col->is_true_sharing = False;
	col->collisions = 1;
	
	insert_last(vector, col);
	
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
	
	//TODO: Maybe remove index-out-of-bounds check
	if (index < 0 || index >= vector->count)
		FG_ERROR("index out of bounds");
	
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

static void free_vector_shallow(vector_t* vector)
{
	VG_(free)(vector->data);
	VG_(free)(vector);
}

/* Accesses pure vector elements but it's not a vector function, put it here for now */
static void remove_thread_accesses(vector_t* vector, ThreadId tid)
{
	if (vector->count <= tid)
		return;
		
	//VG_(printf)("trying to clean tid %d with count %llu\n", tid, ((vector_t*)vector->data[tid])->count);
	
	//TODO: implement cleaning
	//free_vector(vector->data[tid]);	
	free_vector_shallow(vector->data[tid]);
	vector->data[tid] = new_vector();
}

/*
****************
GLOBAL VARIABLES
****************
*/

//	TODO: Replace these with e.g hash tables
static vector_t*	g_accesses;
static vector_t*	g_results;
static vector_t*	g_filenames;
static vector_t*	g_all_memory_accesses;

// TODO: Make these local, but needs some editing
static HChar*		g_filename;
static HChar*		g_dirname;

// TODO: Make this generic to processor somehow, read /proc/cpuinfo or something
static UInt			g_cache_alignment = 64;

static ThreadId		g_current_thread_id;	/* Keeps track of which id the next created thread should have since ll_create gives parent thread id */

/*
****************
HELPER FUNCTIONS
****************
*/

static HChar* fg_get_name_from_id(UInt id)
{
	if (id < 0)
		FG_ERROR("id < 0");
		
	if (id >= size(g_filenames))
		FG_ERROR("id >= size(g_filenames)");
		
	return get(g_filenames, id, False);
}

static Int fg_get_id_from_name(HChar* name)
{
	Int id;
	
	if (name == NULL)
		FG_ERROR("name == NULL");
		
	id = contains_get_index(g_filenames, name);
		
	return id;
}

static Bool fg_is_store(memory_access_e_t access)
{
	return access == MEM_STORE || access == MEM_STORE_GUARDED;
}

static Bool fg_is_load(memory_access_e_t access)
{
	return !fg_is_store(access);
}

/*
********************
FALSEGRIND FUNCTIONS
********************
*/

static Bool access_in_range(thread_access_t* first, thread_access_t* second)
{
	//VG_(printf)("checking if %lx and %lx is in same cache line at line %u and %u\n", first->addr, second->addr, first->line, second->line);
	
	if (first->addr == second->addr)
		return False;	/* Ignore possible data race, not our job */
		
	if (first->access >= MEM_LOAD && first->access <= MEM_LOAD_GUARDED)
		if (second->access >= MEM_LOAD && second->access <= MEM_LOAD_GUARDED)
			return False;	/* Ignore two loads */
	
	if (first->addr / g_cache_alignment != second->addr / g_cache_alignment)
		return False;	/* Access probably won't load into the same cache block */
	
	if (first->file_nr != second->file_nr)
		return False;	/* Not even same file */
		
	/* Accesses might be in the same cache block */
	return True;
}

/* Check if the false sharing actually was true sharing */
static void check_true_sharing(thread_access_t* new, ThreadId tid)
{
	collision_t*		current;
	thread_access_t*	first;
	thread_access_t*	second;
	unsigned long long	i;
	unsigned long long	count;
	
	// Ignore new if it is a write for now
	if (fg_is_store(new->access))
		return;
		
	count = size(g_results);
	
	// Find the case where new is loading a value previously set to false sharing
	for (i = 0; i < count; i++) {
		current = get(g_results, i, False);
		first = current->first;
		second = current->second;
		
		if (current->is_true_sharing)
			continue;
		
		if (tid == current->fst_tid) {
			// Load was at same thread as current->first
			
			if (fg_is_load(second->access))
				continue;
				
			if (new->addr == second->addr) {
				VG_(printf)("(%u) was actually true sharing!\n", (i + 1));
				current->is_true_sharing = True;
			}
				
		} else if (tid == current->snd_tid) {
			// Load was at same thread as current->second
			
			// Make sure that the other thread actually wrote a value
			if (fg_is_load(first->access))
				continue;
				
			// If the current thread read the value written by the second thread, label this as true sharing
			if (new->addr == first->addr) {
				VG_(printf)("(%u) was actually true sharing!\n", (i + 1));
				current->is_true_sharing = True;
			}
		}
		
		// Otherwise do nothing
	}
}

/*	This is probably slow, opt for hash table later */
static void check_false_sharing(thread_access_t* access, ThreadId tid)
{
	vector_t*			vector;
	thread_access_t*	other_access;
	collision_t*		collision;
	unsigned long long	elements;
	unsigned long long	i;
	unsigned long long	j;
	Bool				added;
	
	check_true_sharing(access, tid);
	
	/* Thread 0 does not exist, starts from 1 */
	for (i = 1; i < size(g_accesses); i++) {
		if (i == tid) // Don't get collisions with same thread
			continue;
			
		vector = get(g_accesses, i, False);
		elements = size(vector);
		
		for (j = 0; j < elements; j++) {
			other_access = get(vector, j, False);
			
			if (!access_in_range(access, other_access))
				continue;
			
			added = insert_last_exclusive_malloc_collision(g_results,
				& (collision_t) {	.first = access,
									.second = other_access,
									.fst_tid = tid,
									.snd_tid = i
								});
			if (added)
				VG_(printf)("(%u) false sharing (\"%s\") (lines %d:%d) (threads %u:%u)\n", size(g_results), fg_get_name_from_id(access->file_nr), access->line, other_access->line, tid, i);
				
			//if (added)
			//	VG_(printf)("added was 1: (thread %u) (store: %d), 2: (thread %u) (store: %d)\n", tid, fg_is_store(access->access), i, fg_is_store(other_access->access));
		}
	}
	
	check_true_sharing(access, tid);
}

static VG_REGPARM(2) void add_to_vector(thread_access_t* access, Addr a)
{
	ThreadId	tid;
	AddrInfo 	ai;
	
	VG_(describe_addr)(a, &ai);
		
	if (!(ai.tag == Addr_BrkSegment || ai.tag == Addr_DataSym))
		return;	/* Only care about global variable access or heap access */
	
	tid = VG_(get_running_tid)();	
	access->addr = a;
	
	insert_last_exclusive_access(get(g_accesses, tid, True), access);
	//insert_last(get(g_accesses, tid, True), access);
	check_false_sharing(access, tid);
}

static void add_access(memory_access_e_t access, IRSB* sb, IRExpr* guard, IRExpr* addr, UInt line)
{
	thread_access_t*	t_a;
	IRDirty*			di;
	IRExpr**			argv;
	
	if (line == 0)
		return;	// Ignore memory access with no line number, can't get any information from them	
		
	t_a = VG_(malloc)("add_access_t_a", sizeof(thread_access_t));
		
	t_a->file_nr = fg_get_id_from_name(g_filename);
	
	if (t_a->file_nr == -1) {
		insert_last_malloc(g_filenames, g_filename);
		t_a->file_nr = size(g_filenames) - 1;
	}
	
	t_a->dir_nr = fg_get_id_from_name(g_dirname);
	
	if (t_a->dir_nr == -1) {
		insert_last_malloc(g_filenames, g_dirname);
		t_a->dir_nr = size(g_filenames) - 1;
	}
	
	t_a->access = access;
	t_a->line = line;
	
	insert_last(g_all_memory_accesses, t_a);
	
	argv = mkIRExprVec_2(mkIRExpr_HWord((HWord)t_a), addr);
	di = unsafeIRDirty_0_N(1, "add_to_vector", VG_(fnptr_to_fnentry)(&add_to_vector), argv);
	
	if (guard)
		di->guard = guard;
		
	addStmtToIRSB(sb, IRStmt_Dirty(di));
}

static void fg_track_thread_create(ThreadId tid)
{
	remove_thread_accesses(g_accesses, ++g_current_thread_id);
}

static void fg_track_thread_exit(ThreadId tid)
{
	g_current_thread_id--;
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
					add_access(MEM_LOAD, sbOut, NULL, st->Ist.WrTmp.data->Iex.Load.addr, line);
					//add_access(MEM_LOAD, sbOut, type, NULL, closure->tid, st->Ist.WrTmp.data->Iex.Load.addr, g_filename, line);
				
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_Store: {
				type = typeOfIRExpr(sbIn->tyenv, st->Ist.Store.addr);
			
				add_access(MEM_STORE, sbOut, NULL, st->Ist.Store.addr, line);
				//add_access(MEM_STORE, sbOut, type, NULL, closure->tid, st->Ist.Store.addr, g_filename, line);
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_StoreG: {
				sg = st->Ist.StoreG.details;
				expr = sg->data;
				type = typeOfIRExpr(sbIn->tyenv, expr);
				
				add_access(MEM_STORE_GUARDED, sbOut, sg->guard, sg->addr, line);
				//add_access(MEM_STORE_GUARDED, sbOut, type, sg->guard, closure->tid, sg->addr, g_filename, line);
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_LoadG: {
				lg = st->Ist.LoadG.details;
				type = Ity_INVALID;
				type_wide = Ity_INVALID;
				
				typeOfIRLoadGOp(lg->cvt, &type_wide, &type);
				add_access(MEM_LOAD_GUARDED, sbOut, lg->guard, lg->addr, line);
				//add_access(MEM_LOAD_GUARDED, sbOut, type, lg->guard, closure->tid, lg->addr, g_filename, line);
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_CAS: {
				cas = st->Ist.CAS.details;
				type = typeOfIRExpr(sbIn->tyenv, cas->dataLo);
				
				add_access(MEM_LOAD, sbOut, NULL, cas->addr, line);
				add_access(MEM_STORE, sbOut, NULL, cas->addr, line);
				//add_access(MEM_LOAD, sbOut, type, NULL, closure->tid, cas->addr, g_filename, line);
				//add_access(MEM_STORE, sbOut, type, NULL, closure->tid, cas->addr, g_filename, line);
				
				if (cas->dataHi != NULL) {
					add_access(MEM_LOAD, sbOut, NULL, cas->addr, line);
					add_access(MEM_STORE, sbOut, NULL, cas->addr, line);
					/*
					add_access(MEM_LOAD, sbOut, type, NULL, closure->tid, cas->addr, g_filename, line);
					add_access(MEM_STORE, sbOut, type, NULL, closure->tid, cas->addr, g_filename, line);
					*/
				}
				
				addStmtToIRSB(sbOut, st);
				break;
			}
			
			case Ist_LLSC: {
				if (st->Ist.LLSC.storedata == NULL) {
					type = typeOfIRTemp(sbIn->tyenv, st->Ist.LLSC.result);
					add_access(MEM_LOAD, sbOut, NULL, st->Ist.LLSC.addr, line);
					//add_access(MEM_LOAD, sbOut, type, NULL, closure->tid, st->Ist.LLSC.addr, g_filename, line);
				} else {
					type = typeOfIRExpr(sbIn->tyenv, st->Ist.LLSC.storedata);
					add_access(MEM_STORE, sbOut, NULL, st->Ist.LLSC.addr, line);
					//add_access(MEM_STORE, sbOut, type, NULL, closure->tid, st->Ist.LLSC.addr, g_filename, line);
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
	collision_t*		collision;
	vector_t*			real_results;
	unsigned long long	i;
	unsigned long long	count;
	unsigned long long	true_sharing;
	
	count = size(g_results);
	true_sharing = 0;
	
	for (i = 0; i < count; i++) {
		collision = get(g_results, i, False);
		
		if (collision->is_true_sharing)
			true_sharing++;
	}
	
	VG_(printf)("Final results: detected %llu false sharings where %llu were true sharing\n", count, true_sharing);
	
	real_results = new_vector();
	
	for (i = 0; i < count; i++) {
		collision = get(g_results, i, False);
		
		if (collision->is_true_sharing || collision->collisions < 10)
			continue;
			
		insert_last_exclusive_collision_final(real_results, get(g_results, i, False));
	}
		
	count = size(real_results);
	
	if (count > 0) {
		VG_(printf)("There are %llu unique real false sharings:\n", count);
		
		for (i = 0; i < count; i++) {
			collision = get(real_results, i, False);
			VG_(printf)("In file \"%s\" at line %u and %u, c: %llu\n", fg_get_name_from_id(collision->first->file_nr), collision->first->line, collision->second->line, collision->collisions);
		}
	}
	
	VG_(printf)("\n");
	
	/*
	count = size(g_accesses);
	
	for (i = 0; i < count; i++) {
		tmp = get(g_accesses, i, False);
		free_vector(tmp);
	}
	*/
	
	free_vector(g_results);
	free_vector(g_all_memory_accesses);
	//free_vector(g_filenames);
	VG_(printf)("Observe that this program ignores stack accesses and using OpenMP probably will result in false positives for true sharing. There can also be false positives for true sharing by e.g getting 1000 collisions for false sharing and one read makes it true sharing instead\n");
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
	
	// Needs to track thread creation & exiting
	VG_(track_pre_thread_ll_create)		(fg_track_thread_create);
	VG_(track_pre_thread_ll_exit)		(fg_track_thread_exit);
	
	g_accesses = new_vector();
	g_results = new_vector();
	g_filenames = new_vector();
	g_all_memory_accesses = new_vector();
}

VG_DETERMINE_INTERFACE_VERSION(fg_pre_clo_init)