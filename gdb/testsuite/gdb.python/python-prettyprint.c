/* This testcase is part of GDB, the GNU debugger.

   Copyright 2008, 2009 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

struct s
{
  int a;
  int *b;
};

struct ss
{
  struct s a;
  struct s b;
};

#ifdef __cplusplus
struct S : public s {
  int zs;
};

struct SS {
  int zss;
  S s;
};

struct SSS
{
  SSS (int x, const S& r);
  int a;
  const S &b;
};
SSS::SSS (int x, const S& r) : a(x), b(r) { }

class VirtualTest 
{ 
 private: 
  int value; 

 public: 
  VirtualTest () 
    { 
      value = 1;
    } 
};

class Vbase1 : public virtual VirtualTest { };
class Vbase2 : public virtual VirtualTest { };
class Vbase3 : public virtual VirtualTest { };

class Derived : public Vbase1, public Vbase2, public Vbase3
{ 
 private: 
  int value; 
  
 public:
  Derived () 
    { 
      value = 2; 
    }
};

#endif

typedef struct string_repr
{
  struct whybother
  {
    const char *contents;
  } whybother;
} string;

/* This lets us avoid malloc.  */
int array[100];

struct container
{
  string name;
  int len;
  int *elements;
};

typedef struct container zzz_type;

string
make_string (const char *s)
{
  string result;
  result.whybother.contents = s;
  return result;
}

zzz_type
make_container (const char *s)
{
  zzz_type result;

  result.name = make_string (s);
  result.len = 0;
  result.elements = 0;

  return result;
}

void
add_item (zzz_type *c, int val)
{
  if (c->len == 0)
    c->elements = array;
  c->elements[c->len] = val;
  ++c->len;
}

void init_s(struct s *s, int a)
{
  s->a = a;
  s->b = &s->a;
}

void init_ss(struct ss *s, int a, int b)
{
  init_s(&s->a, a);
  init_s(&s->b, b);
}

void do_nothing(void)
{
  int c;

  c = 23;			/* Another MI breakpoint */
}

int
main ()
{
  struct ss  ss;
  struct ss  ssa[2];
  string x = make_string ("this is x");
  zzz_type c = make_container ("container");
  const struct string_repr cstring = { { "const string" } };

  init_ss(&ss, 1, 2);
  init_ss(ssa+0, 3, 4);
  init_ss(ssa+1, 5, 6);

#ifdef __cplusplus
  S cps;

  cps.zs = 7;
  init_s(&cps, 8);

  SS cpss;
  cpss.zss = 9;
  init_s(&cpss.s, 10);

  SS cpssa[2];
  cpssa[0].zss = 11;
  init_s(&cpssa[0].s, 12);
  cpssa[1].zss = 13;
  init_s(&cpssa[1].s, 14);

  SSS sss(15, cps);

  SSS& ref (sss);

  Derived derived;
  
#endif

  add_item (&c, 23);		/* MI breakpoint here */
  add_item (&c, 72);

#ifdef MI
  do_nothing ();
#endif

  return 0;      /* break to inspect struct and union */
}