/*
** $Id: lstring.c,v 2.8.1.1 2007/12/27 13:02:25 roberto Exp $
** String table (keeps all strings handled by Lua)
** 字符串操作
** 1、在Lua虚拟机中存在一个全局的数据区，用来存放当前系统中的所有字符串。
** 2、同一个字符串数据，在Lua虚拟机中只可能有一份副本，一个字符串一旦创建，将是不可变更的。
** 3、变量存放的仅是字符串的引用，而不是其实际内容。
** See Copyright Notice in lua.h
*/


#include <string.h>

#define lstring_c
#define LUA_CORE

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"

/*
  对保存string的hash桶进行resize
  字符串使用散列桶来存放数据，当数据量非常大时，分配到每个桶上的数据也会非常多，这样一次查找也退化成了一次线性查找过程。
  所以，在Lua中，当字符串数据非常多时，会重新分配桶的数量，降低每个桶上分配到的数据量。
*/
void luaS_resize (lua_State *L, int newsize) {
  GCObject **newhash;
  stringtable *tb;
  int i;
  /* 如果当前GC处于回收字符串数据的阶段，直接返回，不进行重新散列的操作 */
  if (G(L)->gcstate == GCSsweepstring) 
    return;

  /* 重新分配一个散列桶，并且清空 */
  newhash = luaM_newvector(L, newsize, GCObject *);
  tb = &G(L)->strt;
  for (i=0; i<newsize; i++) newhash[i] = NULL;

  /* rehash（重新散列） */
  for (i=0; i<tb->size; i++) {
    GCObject *p = tb->hash[i];
    while (p) {  /* for each node in the list */
      GCObject *next = p->gch.next;  /* save next */
      unsigned int h = gco2ts(p)->hash;
      // 重新计算hash桶索引，这次需要mod新的hash桶大小
      int h1 = lmod(h, newsize);  /* new position */
      lua_assert(cast_int(h%newsize) == lmod(h, newsize));
      p->gch.next = newhash[h1];  /* chain it */
      newhash[h1] = p;
      p = next;
    }
  }
  /* 释放旧的散列桶，保存新分配的散列桶数据 */
  luaM_freearray(L, tb->hash, tb->size, TString *);
  tb->size = newsize;
  tb->hash = newhash;
}

/* 创建一个新的字符串 */
static TString *newlstr (lua_State *L, const char *str, size_t l, unsigned int h) {
  TString *ts;
  stringtable *tb;
  if (l+1 > (MAX_SIZET - sizeof(TString))/sizeof(char))
    luaM_toobig(L);
  ts = cast(TString *, luaM_malloc(L, (l+1)*sizeof(char)+sizeof(TString)));
  ts->tsv.len = l;
  ts->tsv.hash = h;
  ts->tsv.marked = luaC_white(G(L));
  ts->tsv.tt = LUA_TSTRING;
  ts->tsv.reserved = 0;
  memcpy(ts+1, str, l*sizeof(char));
  ((char *)(ts+1))[l] = '\0';  /* ending 0 */
  tb = &G(L)->strt;
  h = lmod(h, tb->size);
  ts->tsv.next = tb->hash[h];  /* chain new entry */
  tb->hash[h] = obj2gco(ts);
  tb->nuse++;
  // 在hash桶数组大小小于MAX_INT/2的情况下，
  // 只要字符串数量大于桶数组数量就开始成倍的扩充桶的容量
  if (tb->nuse > cast(lu_int32, tb->size) && tb->size <= MAX_INT/2)
    luaS_resize(L, tb->size*2);  /* too crowded */
  return ts;
}

/*
  1、计算需要新创建的字符串对应的散列值
  2、根据散列值找到对应的散列桶，遍历该散列桶的所有元素，如果能够查找到同样的字符串，
  说明之前已经存在相同字符串，此时不需要重新分配一个新的字符串数据，直接返回即可。
  3、如果查找不到相同的字符串，调用newlstr函数创建一个新的字符串
*/
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  GCObject *o;
  unsigned int h = cast(unsigned int, l);  /* seed */
  /* 计算散列值操作时的步长。为了在字符串非常大的时候，不需要逐位来进行散列值的计算，而仅需要每步长单位取一个字符就可以了 */
  size_t step = (l>>5)+1;  /* if string is too long, don't hash all its chars */
  size_t l1;
  for (l1=l; l1>=step; l1-=step)  /* compute hash */
    h = h ^ ((h<<5)+(h>>2)+cast(unsigned char, str[l1-1]));
  for (o = G(L)->strt.hash[lmod(h, G(L)->strt.size)];
       o != NULL;
       o = o->gch.next) {
    TString *ts = rawgco2ts(o);
    if (ts->tsv.len == l && (memcmp(str, getstr(ts), l) == 0)) {
      /* 判断这个字符串是否在当前GC阶段被判定为需要回收，如果是，
      则调用changewhite函数修改它的状态，将其改为不需要进行回收，从而达到复用字符串的目的 */
      if (isdead(G(L), o)) changewhite(o);
      return ts;
    }
  }
  return newlstr(L, str, l, h);  /* not found */
}


Udata *luaS_newudata (lua_State *L, size_t s, Table *e) {
  Udata *u;
  if (s > MAX_SIZET - sizeof(Udata))
    luaM_toobig(L);
  u = cast(Udata *, luaM_malloc(L, s + sizeof(Udata)));
  u->uv.marked = luaC_white(G(L));  /* is not finalized */
  u->uv.tt = LUA_TUSERDATA;
  u->uv.len = s;
  u->uv.metatable = NULL;
  u->uv.env = e;
  /* chain it on udata list (after main thread) */
  /*
   这里没有调用luaC_link来挂接新的Udata对象，而是直接把u挂接在mainthread之后。
   从前面的mainstate创建过程可知，mainthread一定是GCObject链表上的最后一个节点（除Udata外）。这是因为挂接过程都是向链表头添加的。
   这里，就可以把所有userdata全部挂接在其他类型之后。这么做的理由是，所有userdata都可能有gc方法（其它类型则没有）。需要统一取调用
   这些gc方法，则应该有一个途径来单独遍历所有的userdata。除此之外，userdata和其它GCObject的处理方式则没有区别，所以依旧挂接在整个
   GCObject链表上而不需要单独在分出一个链表。
   */
  u->uv.next = G(L)->mainthread->next;
  G(L)->mainthread->next = obj2gco(u);
  return u;
}

