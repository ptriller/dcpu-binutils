/* DWARF 2 Expression Evaluator.

   Copyright (C) 2001, 2002, 2003, 2005, 2007, 2008, 2009, 2010, 2011
   Free Software Foundation, Inc.

   Contributed by Daniel Berlin (dan@dberlin.org)

   This file is part of GDB.

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

#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "value.h"
#include "gdbcore.h"
#include "dwarf2.h"
#include "dwarf2expr.h"
#include "gdb_assert.h"

/* Local prototypes.  */

static void execute_stack_op (struct dwarf_expr_context *,
			      const gdb_byte *, const gdb_byte *);

/* Cookie for gdbarch data.  */

static struct gdbarch_data *dwarf_arch_cookie;

/* This holds gdbarch-specific types used by the DWARF expression
   evaluator.  See comments in execute_stack_op.  */

struct dwarf_gdbarch_types
{
  struct type *dw_types[3];
};

/* Allocate and fill in dwarf_gdbarch_types for an arch.  */

static void *
dwarf_gdbarch_types_init (struct gdbarch *gdbarch)
{
  struct dwarf_gdbarch_types *types
    = GDBARCH_OBSTACK_ZALLOC (gdbarch, struct dwarf_gdbarch_types);

  /* The types themselves are lazily initialized.  */

  return types;
}

/* Return the type used for DWARF operations where the type is
   unspecified in the DWARF spec.  Only certain sizes are
   supported.  */

static struct type *
dwarf_expr_address_type (struct dwarf_expr_context *ctx)
{
  struct dwarf_gdbarch_types *types = gdbarch_data (ctx->gdbarch,
						    dwarf_arch_cookie);
  int ndx;

  if (ctx->addr_size == 2)
    ndx = 0;
  else if (ctx->addr_size == 4)
    ndx = 1;
  else if (ctx->addr_size == 8)
    ndx = 2;
  else
    error (_("Unsupported address size in DWARF expressions: %d bits"),
	   8 * ctx->addr_size);

  if (types->dw_types[ndx] == NULL)
    types->dw_types[ndx]
      = arch_integer_type (ctx->gdbarch,
			   8 * ctx->addr_size,
			   0, "<signed DWARF address type>");

  return types->dw_types[ndx];
}

/* Create a new context for the expression evaluator.  */

struct dwarf_expr_context *
new_dwarf_expr_context (void)
{
  struct dwarf_expr_context *retval;

  retval = xcalloc (1, sizeof (struct dwarf_expr_context));
  retval->stack_len = 0;
  retval->stack_allocated = 10;
  retval->stack = xmalloc (retval->stack_allocated
			   * sizeof (struct dwarf_stack_value));
  retval->num_pieces = 0;
  retval->pieces = 0;
  retval->max_recursion_depth = 0x100;
  return retval;
}

/* Release the memory allocated to CTX.  */

void
free_dwarf_expr_context (struct dwarf_expr_context *ctx)
{
  xfree (ctx->stack);
  xfree (ctx->pieces);
  xfree (ctx);
}

/* Helper for make_cleanup_free_dwarf_expr_context.  */

static void
free_dwarf_expr_context_cleanup (void *arg)
{
  free_dwarf_expr_context (arg);
}

/* Return a cleanup that calls free_dwarf_expr_context.  */

struct cleanup *
make_cleanup_free_dwarf_expr_context (struct dwarf_expr_context *ctx)
{
  return make_cleanup (free_dwarf_expr_context_cleanup, ctx);
}

/* Expand the memory allocated to CTX's stack to contain at least
   NEED more elements than are currently used.  */

static void
dwarf_expr_grow_stack (struct dwarf_expr_context *ctx, size_t need)
{
  if (ctx->stack_len + need > ctx->stack_allocated)
    {
      size_t newlen = ctx->stack_len + need + 10;

      ctx->stack = xrealloc (ctx->stack,
			     newlen * sizeof (struct dwarf_stack_value));
      ctx->stack_allocated = newlen;
    }
}

/* Push VALUE onto CTX's stack.  */

static void
dwarf_expr_push (struct dwarf_expr_context *ctx, struct value *value,
		 int in_stack_memory)
{
  struct dwarf_stack_value *v;

  dwarf_expr_grow_stack (ctx, 1);
  v = &ctx->stack[ctx->stack_len++];
  v->value = value;
  v->in_stack_memory = in_stack_memory;
}

/* Push VALUE onto CTX's stack.  */

void
dwarf_expr_push_address (struct dwarf_expr_context *ctx, CORE_ADDR value,
			 int in_stack_memory)
{
  dwarf_expr_push (ctx,
		   value_from_ulongest (dwarf_expr_address_type (ctx), value),
		   in_stack_memory);
}

/* Pop the top item off of CTX's stack.  */

static void
dwarf_expr_pop (struct dwarf_expr_context *ctx)
{
  if (ctx->stack_len <= 0)
    error (_("dwarf expression stack underflow"));
  ctx->stack_len--;
}

/* Retrieve the N'th item on CTX's stack.  */

struct value *
dwarf_expr_fetch (struct dwarf_expr_context *ctx, int n)
{
  if (ctx->stack_len <= n)
     error (_("Asked for position %d of stack, "
	      "stack only has %d elements on it."),
	    n, ctx->stack_len);
  return ctx->stack[ctx->stack_len - (1 + n)].value;
}

/* Require that TYPE be an integral type; throw an exception if not.  */

static void
dwarf_require_integral (struct type *type)
{
  if (TYPE_CODE (type) != TYPE_CODE_INT
      && TYPE_CODE (type) != TYPE_CODE_CHAR
      && TYPE_CODE (type) != TYPE_CODE_BOOL)
    error (_("integral type expected in DWARF expression"));
}

/* Return the unsigned form of TYPE.  TYPE is necessarily an integral
   type.  */

static struct type *
get_unsigned_type (struct gdbarch *gdbarch, struct type *type)
{
  switch (TYPE_LENGTH (type))
    {
    case 1:
      return builtin_type (gdbarch)->builtin_uint8;
    case 2:
      return builtin_type (gdbarch)->builtin_uint16;
    case 4:
      return builtin_type (gdbarch)->builtin_uint32;
    case 8:
      return builtin_type (gdbarch)->builtin_uint64;
    default:
      error (_("no unsigned variant found for type, while evaluating "
	       "DWARF expression"));
    }
}

/* Return the signed form of TYPE.  TYPE is necessarily an integral
   type.  */

static struct type *
get_signed_type (struct gdbarch *gdbarch, struct type *type)
{
  switch (TYPE_LENGTH (type))
    {
    case 1:
      return builtin_type (gdbarch)->builtin_int8;
    case 2:
      return builtin_type (gdbarch)->builtin_int16;
    case 4:
      return builtin_type (gdbarch)->builtin_int32;
    case 8:
      return builtin_type (gdbarch)->builtin_int64;
    default:
      error (_("no signed variant found for type, while evaluating "
	       "DWARF expression"));
    }
}

/* Retrieve the N'th item on CTX's stack, converted to an address.  */

CORE_ADDR
dwarf_expr_fetch_address (struct dwarf_expr_context *ctx, int n)
{
  struct value *result_val = dwarf_expr_fetch (ctx, n);
  enum bfd_endian byte_order = gdbarch_byte_order (ctx->gdbarch);
  ULONGEST result;

  dwarf_require_integral (value_type (result_val));
  result = extract_unsigned_integer (value_contents (result_val),
				     TYPE_LENGTH (value_type (result_val)),
				     byte_order);

  /* For most architectures, calling extract_unsigned_integer() alone
     is sufficient for extracting an address.  However, some
     architectures (e.g. MIPS) use signed addresses and using
     extract_unsigned_integer() will not produce a correct
     result.  Make sure we invoke gdbarch_integer_to_address()
     for those architectures which require it.  */
  if (gdbarch_integer_to_address_p (ctx->gdbarch))
    {
      gdb_byte *buf = alloca (ctx->addr_size);
      struct type *int_type = get_unsigned_type (ctx->gdbarch,
						 value_type (result_val));

      store_unsigned_integer (buf, ctx->addr_size, byte_order, result);
      return gdbarch_integer_to_address (ctx->gdbarch, int_type, buf);
    }

  return (CORE_ADDR) result;
}

/* Retrieve the in_stack_memory flag of the N'th item on CTX's stack.  */

int
dwarf_expr_fetch_in_stack_memory (struct dwarf_expr_context *ctx, int n)
{
  if (ctx->stack_len <= n)
     error (_("Asked for position %d of stack, "
	      "stack only has %d elements on it."),
	    n, ctx->stack_len);
  return ctx->stack[ctx->stack_len - (1 + n)].in_stack_memory;
}

/* Return true if the expression stack is empty.  */

static int
dwarf_expr_stack_empty_p (struct dwarf_expr_context *ctx)
{
  return ctx->stack_len == 0;
}

/* Add a new piece to CTX's piece list.  */
static void
add_piece (struct dwarf_expr_context *ctx, ULONGEST size, ULONGEST offset)
{
  struct dwarf_expr_piece *p;

  ctx->num_pieces++;

  ctx->pieces = xrealloc (ctx->pieces,
			  (ctx->num_pieces
			   * sizeof (struct dwarf_expr_piece)));

  p = &ctx->pieces[ctx->num_pieces - 1];
  p->location = ctx->location;
  p->size = size;
  p->offset = offset;

  if (p->location == DWARF_VALUE_LITERAL)
    {
      p->v.literal.data = ctx->data;
      p->v.literal.length = ctx->len;
    }
  else if (dwarf_expr_stack_empty_p (ctx))
    {
      p->location = DWARF_VALUE_OPTIMIZED_OUT;
      /* Also reset the context's location, for our callers.  This is
	 a somewhat strange approach, but this lets us avoid setting
	 the location to DWARF_VALUE_MEMORY in all the individual
	 cases in the evaluator.  */
      ctx->location = DWARF_VALUE_OPTIMIZED_OUT;
    }
  else if (p->location == DWARF_VALUE_MEMORY)
    {
      p->v.mem.addr = dwarf_expr_fetch_address (ctx, 0);
      p->v.mem.in_stack_memory = dwarf_expr_fetch_in_stack_memory (ctx, 0);
    }
  else if (p->location == DWARF_VALUE_IMPLICIT_POINTER)
    {
      p->v.ptr.die = ctx->len;
      p->v.ptr.offset = value_as_long (dwarf_expr_fetch (ctx, 0));
    }
  else if (p->location == DWARF_VALUE_REGISTER)
    p->v.regno = value_as_long (dwarf_expr_fetch (ctx, 0));
  else
    {
      p->v.value = dwarf_expr_fetch (ctx, 0);
    }
}

/* Evaluate the expression at ADDR (LEN bytes long) using the context
   CTX.  */

void
dwarf_expr_eval (struct dwarf_expr_context *ctx, const gdb_byte *addr,
		 size_t len)
{
  int old_recursion_depth = ctx->recursion_depth;

  execute_stack_op (ctx, addr, addr + len);

  /* CTX RECURSION_DEPTH becomes invalid if an exception was thrown here.  */

  gdb_assert (ctx->recursion_depth == old_recursion_depth);
}

/* Decode the unsigned LEB128 constant at BUF into the variable pointed to
   by R, and return the new value of BUF.  Verify that it doesn't extend
   past BUF_END.  */

const gdb_byte *
read_uleb128 (const gdb_byte *buf, const gdb_byte *buf_end, ULONGEST * r)
{
  unsigned shift = 0;
  ULONGEST result = 0;
  gdb_byte byte;

  while (1)
    {
      if (buf >= buf_end)
	error (_("read_uleb128: Corrupted DWARF expression."));

      byte = *buf++;
      result |= ((ULONGEST) (byte & 0x7f)) << shift;
      if ((byte & 0x80) == 0)
	break;
      shift += 7;
    }
  *r = result;
  return buf;
}

/* Decode the signed LEB128 constant at BUF into the variable pointed to
   by R, and return the new value of BUF.  Verify that it doesn't extend
   past BUF_END.  */

const gdb_byte *
read_sleb128 (const gdb_byte *buf, const gdb_byte *buf_end, LONGEST * r)
{
  unsigned shift = 0;
  LONGEST result = 0;
  gdb_byte byte;

  while (1)
    {
      if (buf >= buf_end)
	error (_("read_sleb128: Corrupted DWARF expression."));

      byte = *buf++;
      result |= ((ULONGEST) (byte & 0x7f)) << shift;
      shift += 7;
      if ((byte & 0x80) == 0)
	break;
    }
  if (shift < (sizeof (*r) * 8) && (byte & 0x40) != 0)
    result |= -(1 << shift);

  *r = result;
  return buf;
}


/* Check that the current operator is either at the end of an
   expression, or that it is followed by a composition operator.  */

void
dwarf_expr_require_composition (const gdb_byte *op_ptr, const gdb_byte *op_end,
				const char *op_name)
{
  /* It seems like DW_OP_GNU_uninit should be handled here.  However,
     it doesn't seem to make sense for DW_OP_*_value, and it was not
     checked at the other place that this function is called.  */
  if (op_ptr != op_end && *op_ptr != DW_OP_piece && *op_ptr != DW_OP_bit_piece)
    error (_("DWARF-2 expression error: `%s' operations must be "
	     "used either alone or in conjunction with DW_OP_piece "
	     "or DW_OP_bit_piece."),
	   op_name);
}

/* Return true iff the types T1 and T2 are "the same".  This only does
   checks that might reasonably be needed to compare DWARF base
   types.  */

static int
base_types_equal_p (struct type *t1, struct type *t2)
{
  if (TYPE_CODE (t1) != TYPE_CODE (t2))
    return 0;
  if (TYPE_UNSIGNED (t1) != TYPE_UNSIGNED (t2))
    return 0;
  return TYPE_LENGTH (t1) == TYPE_LENGTH (t2);
}

/* A convenience function to call get_base_type on CTX and return the
   result.  DIE is the DIE whose type we need.  SIZE is non-zero if
   this function should verify that the resulting type has the correct
   size.  */

static struct type *
dwarf_get_base_type (struct dwarf_expr_context *ctx, ULONGEST die, int size)
{
  struct type *result;

  if (ctx->get_base_type)
    {
      result = ctx->get_base_type (ctx, die);
      if (result == NULL)
	error (_("Could not find type for DW_OP_GNU_const_type"));
      if (size != 0 && TYPE_LENGTH (result) != size)
	error (_("DW_OP_GNU_const_type has different sizes for type and data"));
    }
  else
    /* Anything will do.  */
    result = builtin_type (ctx->gdbarch)->builtin_int;

  return result;
}

/* The engine for the expression evaluator.  Using the context in CTX,
   evaluate the expression between OP_PTR and OP_END.  */

static void
execute_stack_op (struct dwarf_expr_context *ctx,
		  const gdb_byte *op_ptr, const gdb_byte *op_end)
{
  enum bfd_endian byte_order = gdbarch_byte_order (ctx->gdbarch);
  /* Old-style "untyped" DWARF values need special treatment in a
     couple of places, specifically DW_OP_mod and DW_OP_shr.  We need
     a special type for these values so we can distinguish them from
     values that have an explicit type, because explicitly-typed
     values do not need special treatment.  This special type must be
     different (in the `==' sense) from any base type coming from the
     CU.  */
  struct type *address_type = dwarf_expr_address_type (ctx);

  ctx->location = DWARF_VALUE_MEMORY;
  ctx->initialized = 1;  /* Default is initialized.  */

  if (ctx->recursion_depth > ctx->max_recursion_depth)
    error (_("DWARF-2 expression error: Loop detected (%d)."),
	   ctx->recursion_depth);
  ctx->recursion_depth++;

  while (op_ptr < op_end)
    {
      enum dwarf_location_atom op = *op_ptr++;
      ULONGEST result;
      /* Assume the value is not in stack memory.
	 Code that knows otherwise sets this to 1.
	 Some arithmetic on stack addresses can probably be assumed to still
	 be a stack address, but we skip this complication for now.
	 This is just an optimization, so it's always ok to punt
	 and leave this as 0.  */
      int in_stack_memory = 0;
      ULONGEST uoffset, reg;
      LONGEST offset;
      struct value *result_val = NULL;

      switch (op)
	{
	case DW_OP_lit0:
	case DW_OP_lit1:
	case DW_OP_lit2:
	case DW_OP_lit3:
	case DW_OP_lit4:
	case DW_OP_lit5:
	case DW_OP_lit6:
	case DW_OP_lit7:
	case DW_OP_lit8:
	case DW_OP_lit9:
	case DW_OP_lit10:
	case DW_OP_lit11:
	case DW_OP_lit12:
	case DW_OP_lit13:
	case DW_OP_lit14:
	case DW_OP_lit15:
	case DW_OP_lit16:
	case DW_OP_lit17:
	case DW_OP_lit18:
	case DW_OP_lit19:
	case DW_OP_lit20:
	case DW_OP_lit21:
	case DW_OP_lit22:
	case DW_OP_lit23:
	case DW_OP_lit24:
	case DW_OP_lit25:
	case DW_OP_lit26:
	case DW_OP_lit27:
	case DW_OP_lit28:
	case DW_OP_lit29:
	case DW_OP_lit30:
	case DW_OP_lit31:
	  result = op - DW_OP_lit0;
	  result_val = value_from_ulongest (address_type, result);
	  break;

	case DW_OP_addr:
	  result = extract_unsigned_integer (op_ptr,
					     ctx->addr_size, byte_order);
	  op_ptr += ctx->addr_size;
	  /* Some versions of GCC emit DW_OP_addr before
	     DW_OP_GNU_push_tls_address.  In this case the value is an
	     index, not an address.  We don't support things like
	     branching between the address and the TLS op.  */
	  if (op_ptr >= op_end || *op_ptr != DW_OP_GNU_push_tls_address)
	    result += ctx->offset;
	  result_val = value_from_ulongest (address_type, result);
	  break;

	case DW_OP_const1u:
	  result = extract_unsigned_integer (op_ptr, 1, byte_order);
	  result_val = value_from_ulongest (address_type, result);
	  op_ptr += 1;
	  break;
	case DW_OP_const1s:
	  result = extract_signed_integer (op_ptr, 1, byte_order);
	  result_val = value_from_ulongest (address_type, result);
	  op_ptr += 1;
	  break;
	case DW_OP_const2u:
	  result = extract_unsigned_integer (op_ptr, 2, byte_order);
	  result_val = value_from_ulongest (address_type, result);
	  op_ptr += 2;
	  break;
	case DW_OP_const2s:
	  result = extract_signed_integer (op_ptr, 2, byte_order);
	  result_val = value_from_ulongest (address_type, result);
	  op_ptr += 2;
	  break;
	case DW_OP_const4u:
	  result = extract_unsigned_integer (op_ptr, 4, byte_order);
	  result_val = value_from_ulongest (address_type, result);
	  op_ptr += 4;
	  break;
	case DW_OP_const4s:
	  result = extract_signed_integer (op_ptr, 4, byte_order);
	  result_val = value_from_ulongest (address_type, result);
	  op_ptr += 4;
	  break;
	case DW_OP_const8u:
	  result = extract_unsigned_integer (op_ptr, 8, byte_order);
	  result_val = value_from_ulongest (address_type, result);
	  op_ptr += 8;
	  break;
	case DW_OP_const8s:
	  result = extract_signed_integer (op_ptr, 8, byte_order);
	  result_val = value_from_ulongest (address_type, result);
	  op_ptr += 8;
	  break;
	case DW_OP_constu:
	  op_ptr = read_uleb128 (op_ptr, op_end, &uoffset);
	  result = uoffset;
	  result_val = value_from_ulongest (address_type, result);
	  break;
	case DW_OP_consts:
	  op_ptr = read_sleb128 (op_ptr, op_end, &offset);
	  result = offset;
	  result_val = value_from_ulongest (address_type, result);
	  break;

	/* The DW_OP_reg operations are required to occur alone in
	   location expressions.  */
	case DW_OP_reg0:
	case DW_OP_reg1:
	case DW_OP_reg2:
	case DW_OP_reg3:
	case DW_OP_reg4:
	case DW_OP_reg5:
	case DW_OP_reg6:
	case DW_OP_reg7:
	case DW_OP_reg8:
	case DW_OP_reg9:
	case DW_OP_reg10:
	case DW_OP_reg11:
	case DW_OP_reg12:
	case DW_OP_reg13:
	case DW_OP_reg14:
	case DW_OP_reg15:
	case DW_OP_reg16:
	case DW_OP_reg17:
	case DW_OP_reg18:
	case DW_OP_reg19:
	case DW_OP_reg20:
	case DW_OP_reg21:
	case DW_OP_reg22:
	case DW_OP_reg23:
	case DW_OP_reg24:
	case DW_OP_reg25:
	case DW_OP_reg26:
	case DW_OP_reg27:
	case DW_OP_reg28:
	case DW_OP_reg29:
	case DW_OP_reg30:
	case DW_OP_reg31:
	  if (op_ptr != op_end 
	      && *op_ptr != DW_OP_piece
	      && *op_ptr != DW_OP_bit_piece
	      && *op_ptr != DW_OP_GNU_uninit)
	    error (_("DWARF-2 expression error: DW_OP_reg operations must be "
		     "used either alone or in conjunction with DW_OP_piece "
		     "or DW_OP_bit_piece."));

	  result = op - DW_OP_reg0;
	  result_val = value_from_ulongest (address_type, result);
	  ctx->location = DWARF_VALUE_REGISTER;
	  break;

	case DW_OP_regx:
	  op_ptr = read_uleb128 (op_ptr, op_end, &reg);
	  dwarf_expr_require_composition (op_ptr, op_end, "DW_OP_regx");

	  result = reg;
	  result_val = value_from_ulongest (address_type, result);
	  ctx->location = DWARF_VALUE_REGISTER;
	  break;

	case DW_OP_implicit_value:
	  {
	    ULONGEST len;

	    op_ptr = read_uleb128 (op_ptr, op_end, &len);
	    if (op_ptr + len > op_end)
	      error (_("DW_OP_implicit_value: too few bytes available."));
	    ctx->len = len;
	    ctx->data = op_ptr;
	    ctx->location = DWARF_VALUE_LITERAL;
	    op_ptr += len;
	    dwarf_expr_require_composition (op_ptr, op_end,
					    "DW_OP_implicit_value");
	  }
	  goto no_push;

	case DW_OP_stack_value:
	  ctx->location = DWARF_VALUE_STACK;
	  dwarf_expr_require_composition (op_ptr, op_end, "DW_OP_stack_value");
	  goto no_push;

	case DW_OP_GNU_implicit_pointer:
	  {
	    ULONGEST die;
	    LONGEST len;

	    /* The referred-to DIE.  */
	    ctx->len = extract_unsigned_integer (op_ptr, ctx->addr_size,
						 byte_order);
	    op_ptr += ctx->addr_size;

	    /* The byte offset into the data.  */
	    op_ptr = read_sleb128 (op_ptr, op_end, &len);
	    result = (ULONGEST) len;
	    result_val = value_from_ulongest (address_type, result);

	    ctx->location = DWARF_VALUE_IMPLICIT_POINTER;
	    dwarf_expr_require_composition (op_ptr, op_end,
					    "DW_OP_GNU_implicit_pointer");
	  }
	  break;

	case DW_OP_breg0:
	case DW_OP_breg1:
	case DW_OP_breg2:
	case DW_OP_breg3:
	case DW_OP_breg4:
	case DW_OP_breg5:
	case DW_OP_breg6:
	case DW_OP_breg7:
	case DW_OP_breg8:
	case DW_OP_breg9:
	case DW_OP_breg10:
	case DW_OP_breg11:
	case DW_OP_breg12:
	case DW_OP_breg13:
	case DW_OP_breg14:
	case DW_OP_breg15:
	case DW_OP_breg16:
	case DW_OP_breg17:
	case DW_OP_breg18:
	case DW_OP_breg19:
	case DW_OP_breg20:
	case DW_OP_breg21:
	case DW_OP_breg22:
	case DW_OP_breg23:
	case DW_OP_breg24:
	case DW_OP_breg25:
	case DW_OP_breg26:
	case DW_OP_breg27:
	case DW_OP_breg28:
	case DW_OP_breg29:
	case DW_OP_breg30:
	case DW_OP_breg31:
	  {
	    op_ptr = read_sleb128 (op_ptr, op_end, &offset);
	    result = (ctx->read_reg) (ctx->baton, op - DW_OP_breg0);
	    result += offset;
	    result_val = value_from_ulongest (address_type, result);
	  }
	  break;
	case DW_OP_bregx:
	  {
	    op_ptr = read_uleb128 (op_ptr, op_end, &reg);
	    op_ptr = read_sleb128 (op_ptr, op_end, &offset);
	    result = (ctx->read_reg) (ctx->baton, reg);
	    result += offset;
	    result_val = value_from_ulongest (address_type, result);
	  }
	  break;
	case DW_OP_fbreg:
	  {
	    const gdb_byte *datastart;
	    size_t datalen;
	    unsigned int before_stack_len;

	    op_ptr = read_sleb128 (op_ptr, op_end, &offset);
	    /* Rather than create a whole new context, we simply
	       record the stack length before execution, then reset it
	       afterwards, effectively erasing whatever the recursive
	       call put there.  */
	    before_stack_len = ctx->stack_len;
	    /* FIXME: cagney/2003-03-26: This code should be using
               get_frame_base_address(), and then implement a dwarf2
               specific this_base method.  */
	    (ctx->get_frame_base) (ctx->baton, &datastart, &datalen);
	    dwarf_expr_eval (ctx, datastart, datalen);
	    if (ctx->location == DWARF_VALUE_MEMORY)
	      result = dwarf_expr_fetch_address (ctx, 0);
	    else if (ctx->location == DWARF_VALUE_REGISTER)
	      result
		= (ctx->read_reg) (ctx->baton,
				   value_as_long (dwarf_expr_fetch (ctx, 0)));
	    else
	      error (_("Not implemented: computing frame "
		       "base using explicit value operator"));
	    result = result + offset;
	    result_val = value_from_ulongest (address_type, result);
	    in_stack_memory = 1;
	    ctx->stack_len = before_stack_len;
	    ctx->location = DWARF_VALUE_MEMORY;
	  }
	  break;

	case DW_OP_dup:
	  result_val = dwarf_expr_fetch (ctx, 0);
	  in_stack_memory = dwarf_expr_fetch_in_stack_memory (ctx, 0);
	  break;

	case DW_OP_drop:
	  dwarf_expr_pop (ctx);
	  goto no_push;

	case DW_OP_pick:
	  offset = *op_ptr++;
	  result_val = dwarf_expr_fetch (ctx, offset);
	  in_stack_memory = dwarf_expr_fetch_in_stack_memory (ctx, offset);
	  break;
	  
	case DW_OP_swap:
	  {
	    struct dwarf_stack_value t1, t2;

	    if (ctx->stack_len < 2)
	       error (_("Not enough elements for "
			"DW_OP_swap.  Need 2, have %d."),
		      ctx->stack_len);
	    t1 = ctx->stack[ctx->stack_len - 1];
	    t2 = ctx->stack[ctx->stack_len - 2];
	    ctx->stack[ctx->stack_len - 1] = t2;
	    ctx->stack[ctx->stack_len - 2] = t1;
	    goto no_push;
	  }

	case DW_OP_over:
	  result_val = dwarf_expr_fetch (ctx, 1);
	  in_stack_memory = dwarf_expr_fetch_in_stack_memory (ctx, 1);
	  break;

	case DW_OP_rot:
	  {
	    struct dwarf_stack_value t1, t2, t3;

	    if (ctx->stack_len < 3)
	       error (_("Not enough elements for "
			"DW_OP_rot.  Need 3, have %d."),
		      ctx->stack_len);
	    t1 = ctx->stack[ctx->stack_len - 1];
	    t2 = ctx->stack[ctx->stack_len - 2];
	    t3 = ctx->stack[ctx->stack_len - 3];
	    ctx->stack[ctx->stack_len - 1] = t2;
	    ctx->stack[ctx->stack_len - 2] = t3;
	    ctx->stack[ctx->stack_len - 3] = t1;
	    goto no_push;
	  }

	case DW_OP_deref:
	case DW_OP_deref_size:
	case DW_OP_GNU_deref_type:
	  {
	    int addr_size = (op == DW_OP_deref ? ctx->addr_size : *op_ptr++);
	    gdb_byte *buf = alloca (addr_size);
	    CORE_ADDR addr = dwarf_expr_fetch_address (ctx, 0);
	    struct type *type;

	    dwarf_expr_pop (ctx);

	    if (op == DW_OP_GNU_deref_type)
	      {
		ULONGEST type_die;

		op_ptr = read_uleb128 (op_ptr, op_end, &type_die);
		type = dwarf_get_base_type (ctx, type_die, 0);
	      }
	    else
	      type = address_type;

	    (ctx->read_mem) (ctx->baton, buf, addr, addr_size);

	    /* If the size of the object read from memory is different
	       from the type length, we need to zero-extend it.  */
	    if (TYPE_LENGTH (type) != addr_size)
	      {
		ULONGEST result =
		  extract_unsigned_integer (buf, addr_size, byte_order);

		buf = alloca (TYPE_LENGTH (type));
		store_unsigned_integer (buf, TYPE_LENGTH (type),
					byte_order, result);
	      }

	    result_val = value_from_contents_and_address (type, buf, addr);
	    break;
	  }

	case DW_OP_abs:
	case DW_OP_neg:
	case DW_OP_not:
	case DW_OP_plus_uconst:
	  {
	    /* Unary operations.  */
	    result_val = dwarf_expr_fetch (ctx, 0);
	    dwarf_expr_pop (ctx);

	    switch (op)
	      {
	      case DW_OP_abs:
		if (value_less (result_val,
				value_zero (value_type (result_val), not_lval)))
		  result_val = value_neg (result_val);
		break;
	      case DW_OP_neg:
		result_val = value_neg (result_val);
		break;
	      case DW_OP_not:
		dwarf_require_integral (value_type (result_val));
		result_val = value_complement (result_val);
		break;
	      case DW_OP_plus_uconst:
		dwarf_require_integral (value_type (result_val));
		result = value_as_long (result_val);
		op_ptr = read_uleb128 (op_ptr, op_end, &reg);
		result += reg;
		result_val = value_from_ulongest (address_type, result);
		break;
	      }
	  }
	  break;

	case DW_OP_and:
	case DW_OP_div:
	case DW_OP_minus:
	case DW_OP_mod:
	case DW_OP_mul:
	case DW_OP_or:
	case DW_OP_plus:
	case DW_OP_shl:
	case DW_OP_shr:
	case DW_OP_shra:
	case DW_OP_xor:
	case DW_OP_le:
	case DW_OP_ge:
	case DW_OP_eq:
	case DW_OP_lt:
	case DW_OP_gt:
	case DW_OP_ne:
	  {
	    /* Binary operations.  */
	    struct value *first, *second;

	    second = dwarf_expr_fetch (ctx, 0);
	    dwarf_expr_pop (ctx);

	    first = dwarf_expr_fetch (ctx, 0);
	    dwarf_expr_pop (ctx);

	    if (! base_types_equal_p (value_type (first), value_type (second)))
	      error (_("Incompatible types on DWARF stack"));

	    switch (op)
	      {
	      case DW_OP_and:
		dwarf_require_integral (value_type (first));
		dwarf_require_integral (value_type (second));
		result_val = value_binop (first, second, BINOP_BITWISE_AND);
		break;
	      case DW_OP_div:
		result_val = value_binop (first, second, BINOP_DIV);
                break;
	      case DW_OP_minus:
		result_val = value_binop (first, second, BINOP_SUB);
		break;
	      case DW_OP_mod:
		{
		  int cast_back = 0;
		  struct type *orig_type = value_type (first);

		  /* We have to special-case "old-style" untyped values
		     -- these must have mod computed using unsigned
		     math.  */
		  if (orig_type == address_type)
		    {
		      struct type *utype
			= get_unsigned_type (ctx->gdbarch, orig_type);

		      cast_back = 1;
		      first = value_cast (utype, first);
		      second = value_cast (utype, second);
		    }
		  /* Note that value_binop doesn't handle float or
		     decimal float here.  This seems unimportant.  */
		  result_val = value_binop (first, second, BINOP_MOD);
		  if (cast_back)
		    result_val = value_cast (orig_type, result_val);
		}
		break;
	      case DW_OP_mul:
		result_val = value_binop (first, second, BINOP_MUL);
		break;
	      case DW_OP_or:
		dwarf_require_integral (value_type (first));
		dwarf_require_integral (value_type (second));
		result_val = value_binop (first, second, BINOP_BITWISE_IOR);
		break;
	      case DW_OP_plus:
		result_val = value_binop (first, second, BINOP_ADD);
		break;
	      case DW_OP_shl:
		dwarf_require_integral (value_type (first));
		dwarf_require_integral (value_type (second));
		result_val = value_binop (first, second, BINOP_LSH);
		break;
	      case DW_OP_shr:
		dwarf_require_integral (value_type (first));
		dwarf_require_integral (value_type (second));
		if (!TYPE_UNSIGNED (value_type (first)))
		  {
		    struct type *utype
		      = get_unsigned_type (ctx->gdbarch, value_type (first));

		    first = value_cast (utype, first);
		  }

		result_val = value_binop (first, second, BINOP_RSH);
		/* Make sure we wind up with the same type we started
		   with.  */
		if (value_type (result_val) != value_type (second))
		  result_val = value_cast (value_type (second), result_val);
                break;
	      case DW_OP_shra:
		dwarf_require_integral (value_type (first));
		dwarf_require_integral (value_type (second));
		if (TYPE_UNSIGNED (value_type (first)))
		  {
		    struct type *stype
		      = get_signed_type (ctx->gdbarch, value_type (first));

		    first = value_cast (stype, first);
		  }

		result_val = value_binop (first, second, BINOP_RSH);
		/* Make sure we wind up with the same type we started
		   with.  */
		if (value_type (result_val) != value_type (second))
		  result_val = value_cast (value_type (second), result_val);
		break;
	      case DW_OP_xor:
		dwarf_require_integral (value_type (first));
		dwarf_require_integral (value_type (second));
		result_val = value_binop (first, second, BINOP_BITWISE_XOR);
		break;
	      case DW_OP_le:
		/* A <= B is !(B < A).  */
		result = ! value_less (second, first);
		result_val = value_from_ulongest (address_type, result);
		break;
	      case DW_OP_ge:
		/* A >= B is !(A < B).  */
		result = ! value_less (first, second);
		result_val = value_from_ulongest (address_type, result);
		break;
	      case DW_OP_eq:
		result = value_equal (first, second);
		result_val = value_from_ulongest (address_type, result);
		break;
	      case DW_OP_lt:
		result = value_less (first, second);
		result_val = value_from_ulongest (address_type, result);
		break;
	      case DW_OP_gt:
		/* A > B is B < A.  */
		result = value_less (second, first);
		result_val = value_from_ulongest (address_type, result);
		break;
	      case DW_OP_ne:
		result = ! value_equal (first, second);
		result_val = value_from_ulongest (address_type, result);
		break;
	      default:
		internal_error (__FILE__, __LINE__,
				_("Can't be reached."));
	      }
	  }
	  break;

	case DW_OP_call_frame_cfa:
	  result = (ctx->get_frame_cfa) (ctx->baton);
	  result_val = value_from_ulongest (address_type, result);
	  in_stack_memory = 1;
	  break;

	case DW_OP_GNU_push_tls_address:
	  /* Variable is at a constant offset in the thread-local
	  storage block into the objfile for the current thread and
	  the dynamic linker module containing this expression.  Here
	  we return returns the offset from that base.  The top of the
	  stack has the offset from the beginning of the thread
	  control block at which the variable is located.  Nothing
	  should follow this operator, so the top of stack would be
	  returned.  */
	  result = value_as_long (dwarf_expr_fetch (ctx, 0));
	  dwarf_expr_pop (ctx);
	  result = (ctx->get_tls_address) (ctx->baton, result);
	  result_val = value_from_ulongest (address_type, result);
	  break;

	case DW_OP_skip:
	  offset = extract_signed_integer (op_ptr, 2, byte_order);
	  op_ptr += 2;
	  op_ptr += offset;
	  goto no_push;

	case DW_OP_bra:
	  {
	    struct value *val;

	    offset = extract_signed_integer (op_ptr, 2, byte_order);
	    op_ptr += 2;
	    val = dwarf_expr_fetch (ctx, 0);
	    dwarf_require_integral (value_type (val));
	    if (value_as_long (val) != 0)
	      op_ptr += offset;
	    dwarf_expr_pop (ctx);
	  }
	  goto no_push;

	case DW_OP_nop:
	  goto no_push;

        case DW_OP_piece:
          {
            ULONGEST size;

            /* Record the piece.  */
            op_ptr = read_uleb128 (op_ptr, op_end, &size);
	    add_piece (ctx, 8 * size, 0);

            /* Pop off the address/regnum, and reset the location
	       type.  */
	    if (ctx->location != DWARF_VALUE_LITERAL
		&& ctx->location != DWARF_VALUE_OPTIMIZED_OUT)
	      dwarf_expr_pop (ctx);
            ctx->location = DWARF_VALUE_MEMORY;
          }
          goto no_push;

	case DW_OP_bit_piece:
	  {
	    ULONGEST size, offset;

            /* Record the piece.  */
	    op_ptr = read_uleb128 (op_ptr, op_end, &size);
	    op_ptr = read_uleb128 (op_ptr, op_end, &offset);
	    add_piece (ctx, size, offset);

            /* Pop off the address/regnum, and reset the location
	       type.  */
	    if (ctx->location != DWARF_VALUE_LITERAL
		&& ctx->location != DWARF_VALUE_OPTIMIZED_OUT)
	      dwarf_expr_pop (ctx);
            ctx->location = DWARF_VALUE_MEMORY;
	  }
	  goto no_push;

	case DW_OP_GNU_uninit:
	  if (op_ptr != op_end)
	    error (_("DWARF-2 expression error: DW_OP_GNU_uninit must always "
		   "be the very last op."));

	  ctx->initialized = 0;
	  goto no_push;

	case DW_OP_call2:
	  result = extract_unsigned_integer (op_ptr, 2, byte_order);
	  op_ptr += 2;
	  ctx->dwarf_call (ctx, result);
	  goto no_push;

	case DW_OP_call4:
	  result = extract_unsigned_integer (op_ptr, 4, byte_order);
	  op_ptr += 4;
	  ctx->dwarf_call (ctx, result);
	  goto no_push;
	
	case DW_OP_GNU_entry_value:
	  /* This operation is not yet supported by GDB.  */
	  ctx->location = DWARF_VALUE_OPTIMIZED_OUT;
	  ctx->stack_len = 0;
	  ctx->num_pieces = 0;
	  goto abort_expression;

	case DW_OP_GNU_const_type:
	  {
	    ULONGEST type_die;
	    int n;
	    const gdb_byte *data;
	    struct type *type;

	    op_ptr = read_uleb128 (op_ptr, op_end, &type_die);
	    n = *op_ptr++;
	    data = op_ptr;
	    op_ptr += n;

	    type = dwarf_get_base_type (ctx, type_die, n);
	    result_val = value_from_contents (type, data);
	  }
	  break;

	case DW_OP_GNU_regval_type:
	  {
	    ULONGEST type_die;
	    struct type *type;

	    op_ptr = read_uleb128 (op_ptr, op_end, &reg);
	    op_ptr = read_uleb128 (op_ptr, op_end, &type_die);

	    type = dwarf_get_base_type (ctx, type_die, 0);
	    result = (ctx->read_reg) (ctx->baton, reg);
	    result_val = value_from_ulongest (type, result);
	  }
	  break;

	case DW_OP_GNU_convert:
	case DW_OP_GNU_reinterpret:
	  {
	    ULONGEST type_die;
	    struct type *type;

	    op_ptr = read_uleb128 (op_ptr, op_end, &type_die);

	    type = dwarf_get_base_type (ctx, type_die, 0);

	    result_val = dwarf_expr_fetch (ctx, 0);
	    dwarf_expr_pop (ctx);

	    if (op == DW_OP_GNU_convert)
	      result_val = value_cast (type, result_val);
	    else if (type == value_type (result_val))
	      {
		/* Nothing.  */
	      }
	    else if (TYPE_LENGTH (type)
		     != TYPE_LENGTH (value_type (result_val)))
	      error (_("DW_OP_GNU_reinterpret has wrong size"));
	    else
	      result_val
		= value_from_contents (type,
				       value_contents_all (result_val));
	  }
	  break;

	default:
	  error (_("Unhandled dwarf expression opcode 0x%x"), op);
	}

      /* Most things push a result value.  */
      gdb_assert (result_val != NULL);
      dwarf_expr_push (ctx, result_val, in_stack_memory);
    no_push:
      ;
    }

  /* To simplify our main caller, if the result is an implicit
     pointer, then make a pieced value.  This is ok because we can't
     have implicit pointers in contexts where pieces are invalid.  */
  if (ctx->location == DWARF_VALUE_IMPLICIT_POINTER)
    add_piece (ctx, 8 * ctx->addr_size, 0);

abort_expression:
  ctx->recursion_depth--;
  gdb_assert (ctx->recursion_depth >= 0);
}

void
_initialize_dwarf2expr (void)
{
  dwarf_arch_cookie
    = gdbarch_data_register_post_init (dwarf_gdbarch_types_init);
}
