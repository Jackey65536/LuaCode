/*
** $Id: lgc.h,v 2.15.1.1 2007/12/27 13:02:25 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
** Lua采用一个简单的标记清除算法的GC系统
*/

#ifndef lgc_h
#define lgc_h


#include "lobject.h"


/*
** Possible states of the Garbage Collector
** GC的五大阶段（状态），从小到大依次执行
*/
#define GCSpause	    0
#define GCSpropagate	1
/*
 string在lua中是单独管理的，所以也需要单独清除。GCSsweepstring阶段干的就是这个事情。
 stringtable以hash表形式管理所有的string。GCSsweepstring中，每个步骤（step）清理hash表的一列
 */
#define GCSsweepstring	2
/* 对所有未标记的其他GCObject做清理工作 */
#define GCSsweep	    3
/* 如果在前面的阶段发现了需要调用gc元方法的userdata对象，将在这个阶段逐个调用，由函数GCTM负责 */
#define GCSfinalize	    4


/*
** some userful bit tricks
** ~：按位取反
** ^：按位异或
*/
#define resetbits(x,m)	((x) &= cast(lu_byte, ~(m)))
#define setbits(x,m)	((x) |= (m))
#define testbits(x,m)	((x) & (m))
#define bitmask(b)	(1<<(b)) /* 2^b */
#define bit2mask(b1,b2)	(bitmask(b1) | bitmask(b2)) /* 2^b1 | 2^b2 */
#define l_setbit(x,b)	setbits(x, bitmask(b))
#define resetbit(x,b)	resetbits(x, bitmask(b))
#define testbit(x,b)	testbits(x, bitmask(b))
#define set2bits(x,b1,b2)	setbits(x, (bit2mask(b1, b2)))
#define reset2bits(x,b1,b2)	resetbits(x, (bit2mask(b1, b2)))
#define test2bits(x,b1,b2)	testbits(x, (bit2mask(b1, b2)))



/*
 lua认为每个GCObject都有一个颜色。一开始，所有节点都是白色的。新创建出来的节点也被默认设置为白色。
 在标记阶段，可见的节点，逐个被设置为黑色。有些节点比较复杂，它会关联别的节点。在没有处理完所有关联
 节点前，lua认为它是灰色的。
 节点的颜色被存储在GCObject的CommonHeader里，放在marked域中。为了节约内存，以位形式存放。
** Layout for bit use in `marked' field:
** bit 0 - object is white (type 0)
** bit 1 - object is white (type 1)
** bit 2 - object is black
** bit 3 - for userdata: has been finalized
** bit 3 - for tables: has weak keys
** bit 4 - for tables: has weak values
** bit 5 - object is fixed (should not be collected)
** bit 6 - object is "super" fixed (only the main thread)
*/
#define WHITE0BIT	0
#define WHITE1BIT	1
#define BLACKBIT	2
/*
 标记userdata。当userdata确认不被引用，则设置上这个标记。它不同于颜色标记。
 因为userdata由于gc元方法的存在，释放所占内存是需要延迟到gc元方法调用之后的。
 这个标记可以保证元方法不会被反复调用。
 */
#define FINALIZEDBIT	3
// 弱key
#define KEYWEAKBIT	3
// 弱值
#define VALUEWEAKBIT	4
/*
 标记一个GCObject不可以在GC流程中被清除。
 为什么要有这种状态？关键在于lua本身会用到一个字符串，它们有可能不被任何地方引用，
 但在每次接触到这个字符串时，又不希望反复生成。那么，这些字符串就会被保护起来，
 设置上fFIXEDBIT
 */
#define FIXEDBIT	5
/*
 标记主mainthread，也就是一切的起点。我们调用lua_newstate返回的那个结构。
 即使到lua_close的那一刻，这个结构也是不能随意清除的。
 */
#define SFIXEDBIT	6
// 两种白色的或
#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)

// 将mark位与两个白色位进行比较，只要其中一个置位就是白色的
#define iswhite(x)      test2bits((x)->gch.marked, WHITE0BIT, WHITE1BIT)
// 将mark位与黑色位进行比较
#define isblack(x)      testbit((x)->gch.marked, BLACKBIT)
// 既不是白色，也不是黑色
#define isgray(x)	(!isblack(x) && !iswhite(x))
// 不是当前的白色
#define otherwhite(g)	(g->currentwhite ^ WHITEBITS)

// 如果结点的白色是otherwhite，那么就是一个死结点
// 这个函数都是在mark阶段过后使用的,所以此时的otherwhite其实就是本次GC的白色
#define isdead(g,v)	((v)->gch.marked & otherwhite(g) & WHITEBITS)

// 将节点mark为白色,同时清除黑色/灰色等
#define changewhite(x)	((x)->gch.marked ^= WHITEBITS)
#define gray2black(x)	l_setbit((x)->gch.marked, BLACKBIT)

#define valiswhite(x)	(iscollectable(x) && iswhite(gcvalue(x)))

// 返回当前的白色
#define luaC_white(g)	cast(lu_byte, (g)->currentwhite & WHITEBITS)

// 如果大于阙值,就启动一次GC
#define luaC_checkGC(L) { \
  condhardstacktests(luaD_reallocstack(L, L->stacksize - EXTRA_STACK - 1)); \
  if (G(L)->totalbytes >= G(L)->GCthreshold) \
	luaC_step(L); }


#define luaC_barrier(L,p,v) { if (valiswhite(v) && isblack(obj2gco(p)))  \
	luaC_barrierf(L,obj2gco(p),gcvalue(v)); }

// 回退成黑色
#define luaC_barriert(L,t,v) { if (valiswhite(v) && isblack(obj2gco(t)))  \
	luaC_barrierback(L,t); }

#define luaC_objbarrier(L,p,o)  \
	{ if (iswhite(obj2gco(o)) && isblack(obj2gco(p))) \
		luaC_barrierf(L,obj2gco(p),obj2gco(o)); }

#define luaC_objbarriert(L,t,o)  \
   { if (iswhite(obj2gco(o)) && isblack(obj2gco(t))) luaC_barrierback(L,t); }

LUAI_FUNC size_t luaC_separateudata (lua_State *L, int all);
LUAI_FUNC void luaC_callGCTM (lua_State *L);
LUAI_FUNC void luaC_freeall (lua_State *L);
LUAI_FUNC void luaC_step (lua_State *L);
LUAI_FUNC void luaC_fullgc (lua_State *L);
LUAI_FUNC void luaC_link (lua_State *L, GCObject *o, lu_byte tt);
LUAI_FUNC void luaC_linkupval (lua_State *L, UpVal *uv);
LUAI_FUNC void luaC_barrierf (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback (lua_State *L, Table *t);


#endif
