/*
** $Id: lstate.h,v 2.24.1.2 2008/01/03 15:20:39 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"



struct lua_longjmp;  /* defined in ldo.c */


/* table of globals */
#define gt(L)	(&L->l_gt)

/* registry */
#define registry(L)	(&G(L)->l_registry)


/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_CI_SIZE           8

#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/*
  专门用于存放字符串的散列数组
*/
typedef struct stringtable {
  GCObject **hash;
  lu_int32 nuse;  /* number of elements */
  int size;       // hash桶数组大小
} stringtable;


/*
** informations about a call
*/
typedef struct CallInfo {
  StkId base;  /* base for this function */
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  const Instruction *savedpc;
  int nresults;  /* expected number of results from this function */
  int tailcalls;  /* number of tail calls lost under this entry */
} CallInfo;



#define curr_func(L)	(clvalue(L->ci->func))
#define ci_func(ci)	(clvalue((ci)->func))
#define f_isLua(ci)	(!ci_func(ci)->c.isC)
#define isLua(ci)	(ttisfunction((ci)->func) && f_isLua(ci))


/*
** `global state', shared by all threads of this state
*/
typedef struct global_State {
  /*
   所有的string以stringtabled结构保存在stringtable strt域。string的值类型为TString，
   它和其它GCObject一样，拥有CommonHeader，但需要注意，CommonHeader中的next域却和其它
   类型的单向链表意义不同。它被挂接在stringtable这个hash表中。
   */
  stringtable strt;  /* hash table for strings，存放所有的字符串 */
  lua_Alloc frealloc;  /* function to reallocate memory */
  void *ud;         /* auxiliary data to `frealloc' */
  unsigned char currentwhite;
  unsigned char gcstate;  /* state of garbage collector */
  int sweepstrgc;  /* position of sweep in `strt' */
  /* 除string外的GCObject链表头在rootgc域中。初始化时，这个域被初始化为主线程。*/
  GCObject *rootgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* position of sweep in `rootgc' */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of weak tables (to be cleared) */
  // 所有有GC方法的udata都放在tmudata链表中
  GCObject *tmudata;  /* last element of list of userdata to be GC */
  Mbuffer buff;  /* temporary buffer for string concatentation */
  // 一个阈值，当这个totalbytes大于这个阈值时进行自动GC
  lu_mem GCthreshold;
  // 保存当前分配的总内存数量
  lu_mem totalbytes;  /* number of bytes currently allocated */
  // 一个估算值，根据这个计算GCthreshold
  lu_mem estimate;  /* an estimate of number of bytes actually in use */
  // 当前待GC的数据大小，其实就是累加totalbytes和GCthreshold的差值
  lu_mem gcdept;  /* how much GC is `behind schedule' */
  // 可以配置的一个值，不是计算出来的，根据这个计算GCthreshold，以此来控制下一次GC触发的时间
  int gcpause;  /* size of pause between successive GCs */
  // 每次进行GC操作回收的数据比例，见lgc.c/luaC_step函数
  int gcstepmul;  /* GC `granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  TValue l_registry;
  struct lua_State *mainthread;
  UpVal uvhead;  /* head of double-linked list of all open upvalues */
  struct Table *mt[NUM_TAGS];  /* metatables for basic types */
  /* 以TString指针的方式记录了所有元方法的名字 */
  TString *tmname[TM_N];  /* array with tag-method names */
} global_State;


/*
** `per thread' state
** 表示lua vm的某种状态，也是一个类型为thread的GCObject。
*/
struct lua_State {
  CommonHeader;
  unsigned char status;
  StkId top;  /* first free slot in the stack */
  StkId base;  /* base of current function */
  /*
   一个完整的lua虚拟机在运行时，可有多个lua_State，即多个thread。他们会共享一些数据。这些数据放在
   global_State *l_G域中。其中自然也包括所有GCObject的链表。
   */
  global_State *l_G;
  CallInfo *ci;  /* call info for current function */
  const Instruction *savedpc;  /* `savedpc' of current function */
  StkId stack_last;  /* last free slot in the stack */
  StkId stack;  /* stack base */
  CallInfo *end_ci;  /* points after end of ci array*/
  CallInfo *base_ci;  /* array of CallInfo's */
  int stacksize;
  int size_ci;  /* size of array `base_ci' */
  unsigned short nCcalls;  /* number of nested C calls */
  unsigned short baseCcalls;  /* nested C calls when resuming coroutine */
  unsigned char hookmask;
  unsigned char allowhook;
  int basehookcount;
  int hookcount;
  lua_Hook hook;
  TValue l_gt;  /* table of globals */
  TValue env;  /* temporary place for environments */
  GCObject *openupval;  /* list of open upvalues in this stack */
  GCObject *gclist;
  struct lua_longjmp *errorJmp;  /* current error recover point */
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
};


#define G(L)	(L->l_G)


/*
** Union of all collectable objects
** 将所有需要进行垃圾回收的数据类型囊括起来
*/
union GCObject {
  /* GC部分 */
  GCheader gch;
  union TString ts;
  union Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct UpVal uv;
  struct lua_State th;  /* thread */
};


/* macros to convert a GCObject into a specific value */
#define rawgco2ts(o)	check_exp((o)->gch.tt == LUA_TSTRING, &((o)->ts))
#define gco2ts(o)	(&rawgco2ts(o)->tsv)
#define rawgco2u(o)	check_exp((o)->gch.tt == LUA_TUSERDATA, &((o)->u))
#define gco2u(o)	(&rawgco2u(o)->uv)
#define gco2cl(o)	check_exp((o)->gch.tt == LUA_TFUNCTION, &((o)->cl))
#define gco2h(o)	check_exp((o)->gch.tt == LUA_TTABLE, &((o)->h))
#define gco2p(o)	check_exp((o)->gch.tt == LUA_TPROTO, &((o)->p))
#define gco2uv(o)	check_exp((o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define ngcotouv(o) \
	check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, &((o)->uv))
#define gco2th(o)	check_exp((o)->gch.tt == LUA_TTHREAD, &((o)->th))

/* macro to convert any Lua object into a GCObject */
#define obj2gco(v)	(cast(GCObject *, (v)))


LUAI_FUNC lua_State *luaE_newthread (lua_State *L);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);

#endif
