/*
 * Copyright (c) 2001 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#if !defined(WINNT)
#ident "$Id: compile.cc,v 1.35 2001/04/18 04:21:23 steve Exp $"
#endif

# include  "compile.h"
# include  "functor.h"
# include  "symbols.h"
# include  "codes.h"
# include  "schedule.h"
# include  "vpi_priv.h"
# include  "vthread.h"
# include  "parse_misc.h"
# include  <malloc.h>
# include  <stdlib.h>
# include  <string.h>
# include  <assert.h>

unsigned compile_errors = 0;

/*
 * The opcode table lists all the code mnemonics, along with their
 * opcode and operand types. The table is written sorted by mnemonic
 * so that it can be searched by binary search. The opcode_compare
 * function is a helper function for that lookup.
 */

enum operand_e {
	/* Place holder for unused operand */
      OA_NONE,
	/* The operand is a number, an immediate unsigned integer */
      OA_NUMBER,
	/* The operand is a thread bit index */
      OA_BIT1,
      OA_BIT2,
	/* The operand is a pointer to code space */
      OA_CODE_PTR,
	/* The operand is a variable or net pointer */
      OA_FUNC_PTR
};

struct opcode_table_s {
      const char*mnemonic;
      vvp_code_fun opcode;

      unsigned argc;
      enum operand_e argt[OPERAND_MAX];
};

const static struct opcode_table_s opcode_table[] = {
      { "%add",    of_ADD,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%and",    of_AND,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%assign", of_ASSIGN, 3,  {OA_FUNC_PTR, OA_BIT1,     OA_BIT2} },
      { "%cmp/s",  of_CMPS,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%cmp/u",  of_CMPU,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%cmp/x",  of_CMPX,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%cmp/z",  of_CMPZ,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%delay",  of_DELAY,  1,  {OA_NUMBER,   OA_NONE,     OA_NONE} },
      { "%end",    of_END,    0,  {OA_NONE,     OA_NONE,     OA_NONE} },
      { "%inv",    of_INV,    2,  {OA_BIT1,     OA_BIT2,     OA_NONE} },
      { "%jmp",    of_JMP,    1,  {OA_CODE_PTR, OA_NONE,     OA_NONE} },
      { "%jmp/0",  of_JMP0,   2,  {OA_CODE_PTR, OA_BIT1,     OA_NONE} },
      { "%jmp/0xz",of_JMP0XZ, 2,  {OA_CODE_PTR, OA_BIT1,     OA_NONE} },
      { "%jmp/1",  of_JMP1,   2,  {OA_CODE_PTR, OA_BIT1,     OA_NONE} },
      { "%join",   of_JOIN,   0,  {OA_NONE,     OA_NONE,     OA_NONE} },
      { "%load",   of_LOAD,   2,  {OA_BIT1,     OA_FUNC_PTR, OA_NONE} },
      { "%mov",    of_MOV,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%noop",   of_NOOP,   0,  {OA_NONE,     OA_NONE,     OA_NONE} },
      { "%nor/r",  of_NORR,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%or",     of_OR,     3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%set",    of_SET,    2,  {OA_FUNC_PTR, OA_BIT1,     OA_NONE} },
      { "%wait",   of_WAIT,   1,  {OA_FUNC_PTR, OA_NONE,     OA_NONE} },
      { "%xnor",   of_XNOR,   3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { "%xor",    of_XOR,    3,  {OA_BIT1,     OA_BIT2,     OA_NUMBER} },
      { 0, of_NOOP, 0, {OA_NONE, OA_NONE, OA_NONE} }
};

static unsigned opcode_count = 0;

static int opcode_compare(const void*k, const void*r)
{
      const char*kp = (const char*)k;
      const struct opcode_table_s*rp = (const struct opcode_table_s*)r;
      return strcmp(kp, rp->mnemonic);
}

/*
 * Keep a symbol table of addresses within code space. Labels on
 * executable opcodes are mapped to their address here.
 */
static symbol_table_t sym_codespace = 0;

/*
 * Keep a symbol table of functors mentioned in the source. This table
 * is used to resolve references as they come.
 */
static symbol_table_t sym_functors = 0;

/*
 * VPI objects are indexed during compile time so that they can be
 * linked together as they are created. This symbol table matches
 * labels to vpiHandles.
 */
static symbol_table_t sym_vpi = 0;


/*
 * If a functor parameter makes a forward reference to a functor, then
 * I need to save that reference and resolve it after the functors are
 * created. Use this structure to keep the unresolved references in an
 * unsorted singly linked list.
 *
 * The postpone_functor_input arranges for a functor input to be
 * resolved and connected at cleanup. This is used if the symbol is
 * defined after its use in a functor. The ptr parameter is the
 * complete vvp_input_t for the input port.
 */
struct resolv_list_s {
      struct resolv_list_s*next;
      vvp_ipoint_t port;

      char*source;
      unsigned idx;
};

static struct resolv_list_s*resolv_list = 0;

static void postpone_functor_input(vvp_ipoint_t ptr, char*lab, unsigned idx)
{
      struct resolv_list_s*res = (struct resolv_list_s*)
	    calloc(1, sizeof(struct resolv_list_s));

      res->port    = ptr;
      res->source = lab;
      res->idx    = idx;
      res->next   = resolv_list;
      resolv_list = res;
}


/*
 * Instructions may make forward references to labels. In this case,
 * the compile makes one of these to remember to retry the
 * resolution.
 */
struct cresolv_list_s {
      struct cresolv_list_s*next;
      struct vvp_code_s*cp;
      char*lab;
};

static struct cresolv_list_s*cresolv_list = 0;

void compile_vpi_symbol(const char*label, vpiHandle obj)
{
      symbol_value_t val;
      val.ptr = obj;
      sym_set_value(sym_vpi, label, val);
}

/*
 * Initialize the compiler by allocation empty symbol tables and
 * initializing the various address spaces.
 */
void compile_init(void)
{
      sym_vpi = new_symbol_table();
      compile_vpi_symbol("$time", vpip_sim_time());

      sym_functors = new_symbol_table();
      functor_init();

      sym_codespace = new_symbol_table();
      codespace_init();

      opcode_count = 0;
      while (opcode_table[opcode_count].mnemonic)
	    opcode_count += 1;
}

void compile_load_vpi_module(char*name)
{
      vpip_load_module(name, module_path);
      free(name);
}

/*
 * The parser calls this function to create a functor. I allocate a
 * functor, and map the name to the vvp_ipoint_t address for the
 * functor. Also resolve the inputs to the functor.
 */
void compile_functor(char*label, char*type, unsigned init,
		     unsigned argc, struct symb_s*argv)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      { symbol_value_t val;
        val.num = fdx;
	sym_set_value(sym_functors, label, val);
      }

      assert(argc <= 4);

	/* Run through the arguments looking for the functors that are
	   connected to my input ports. For each source functor that I
	   find, connect the output of that functor to the indexed
	   input by inserting myself (complete with the port number in
	   the vvp_ipoint_t) into the list that the source heads.

	   If the source functor is not declared yet, then don't do
	   the link yet. Save the reference to be resolved later. */

      for (unsigned idx = 0 ;  idx < argc ;  idx += 1) {
	    symbol_value_t val = sym_get_value(sym_functors, argv[idx].text);
	    vvp_ipoint_t tmp = val.num;

	    if (tmp) {
		  tmp = ipoint_index(tmp, argv[idx].idx);
		  functor_t fport = functor_index(tmp);
		  obj->port[idx] = fport->out;
		  fport->out = ipoint_make(fdx, idx);

		  free(argv[idx].text);

	    } else {
		  postpone_functor_input(ipoint_make(fdx, idx),
					 argv[idx].text,
					 argv[idx].idx);

	    }
      }

      obj->ival = init;
      obj->oval = 2;
      obj->mode = 0;

      if (strcmp(type, "OR") == 0) {
	    obj->table = ft_OR;

      } else if (strcmp(type, "AND") == 0) {
	    obj->table = ft_AND;

      } else if (strcmp(type, "BUF") == 0) {
	    obj->table = ft_BUF;

      } else if (strcmp(type, "NOR") == 0) {
	    obj->table = ft_NOR;

      } else if (strcmp(type, "NOT") == 0) {
	    obj->table = ft_NOT;

      } else if (strcmp(type, "XOR") == 0) {
	    obj->table = ft_XOR;

      } else {
	    yyerror("invalid functor type.");
      }

      free(argv);
      free(label);
      free(type);
}

void compile_event(char*label, char*type,
		   unsigned argc, struct symb_s*argv)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      { symbol_value_t val;
        val.num = fdx;
	sym_set_value(sym_functors, label, val);
      }

      assert(argc <= 4);

	/* Run through the arguments looking for the functors that are
	   connected to my input ports. For each source functor that I
	   find, connect the output of that functor to the indexed
	   input by inserting myself (complete with the port number in
	   the vvp_ipoint_t) into the list that the source heads.

	   If the source functor is not declared yet, then don't do
	   the link yet. Save the reference to be resolved later. */

      for (unsigned idx = 0 ;  idx < argc ;  idx += 1) {
	    symbol_value_t val = sym_get_value(sym_functors, argv[idx].text);
	    vvp_ipoint_t tmp = val.num;

	    if (tmp) {
		  tmp = ipoint_index(tmp, argv[idx].idx);
		  functor_t fport = functor_index(tmp);
		  obj->port[idx] = fport->out;
		  fport->out = ipoint_make(fdx, idx);

		  free(argv[idx].text);

	    } else {
		  postpone_functor_input(ipoint_make(fdx, idx),
					 argv[idx].text,
					 argv[idx].idx);

	    }
      }

      free(argv);

      obj->ival = 0xaa;
      obj->oval = 2;
      obj->mode = 1;
      obj->out  = 0;

      obj->event = (struct vvp_event_s*) malloc(sizeof (struct vvp_event_s));
      obj->event->threads = 0;
      obj->event->ival = obj->ival;

      if (strcmp(type,"posedge") == 0)
	    obj->event->vvp_edge_tab = vvp_edge_posedge;
      else if (strcmp(type,"negedge") == 0)
	    obj->event->vvp_edge_tab = vvp_edge_negedge;
      else if (strcmp(type,"edge") == 0)
	    obj->event->vvp_edge_tab = vvp_edge_anyedge;
      else
	    obj->event->vvp_edge_tab = 0;

      free(type);
      free(label);
}

void compile_named_event(char*label, char*name)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      { symbol_value_t val;
        val.num = fdx;
	sym_set_value(sym_functors, label, val);
      }

      obj->ival = 0xaa;
      obj->oval = 2;
      obj->mode = 2;
      obj->out  = 0;

      obj->event = (struct vvp_event_s*) malloc(sizeof (struct vvp_event_s));
      obj->event->threads = 0;
      obj->event->ival = obj->ival;

      free(label);
      free(name);
}

void compile_event_or(char*label, unsigned argc, struct symb_s*argv)
{
      vvp_ipoint_t fdx = functor_allocate(1);
      functor_t obj = functor_index(fdx);

      { symbol_value_t val;
        val.num = fdx;
	sym_set_value(sym_functors, label, val);
      }

      obj->ival = 0xaa;
      obj->oval = 2;
      obj->mode = 2;
      obj->out  = 0;

      obj->event = new struct vvp_event_s;
      obj->event->threads = 0;
      obj->event->ival = obj->ival;

	/* Link the outputs of the named events to me. */

      for (unsigned idx = 0 ;  idx < argc ;  idx += 1) {
	    symbol_value_t val = sym_get_value(sym_functors, argv[idx].text);
	    vvp_ipoint_t tmp = val.num;

	    assert(tmp);

	    tmp = ipoint_index(tmp, argv[idx].idx);

	    functor_t fport = functor_index(tmp);
	    assert(fport->out == 0);
	    fport->out = fdx;

	    free(argv[idx].text);

      }

      free(argv);

      free(label);
}

/*
 * The parser uses this function to compile an link an executable
 * opcode. I do this by looking up the opcode in the opcode_table. The
 * table gives the operand structure that is acceptible, so I can
 * process the operands here as well.
 */
void compile_code(char*label, char*mnem, comp_operands_t opa)
{
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
	    free(label);
      }

	/* Lookup the opcode in the opcode table. */
      struct opcode_table_s*op = (struct opcode_table_s*)
	    bsearch(mnem, opcode_table, opcode_count,
		    sizeof(struct opcode_table_s), &opcode_compare);
      if (op == 0) {
	    yyerror("Invalid opcode");
	    return;
      }

      assert(op);

	/* Build up the code from the information about the opcode and
	   the information from the comiler. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = op->opcode;

      if (op->argc != (opa? opa->argc : 0)) {
	    yyerror("operand count");
	    return;
      }

	/* Pull the operands that the instruction expects from the
	   list that the parser supplied. */

      for (unsigned idx = 0 ;  idx < op->argc ;  idx += 1) {
	    symbol_value_t tmp;

	    switch (op->argt[idx]) {
		case OA_NONE:
		  break;

		case OA_BIT1:
		  if (opa->argv[idx].ltype != L_NUMB) {
			yyerror("operand format");
			break;
		  }

		  code->bit_idx1 = opa->argv[idx].numb;
		  break;

		case OA_BIT2:
		  if (opa->argv[idx].ltype != L_NUMB) {
			yyerror("operand format");
			break;
		  }

		  code->bit_idx2 = opa->argv[idx].numb;
		  break;

		case OA_CODE_PTR:
		  if (opa->argv[idx].ltype != L_SYMB) {
			yyerror("operand format");
			break;
		  }

		  assert(opa->argv[idx].symb.idx == 0);
		  tmp = sym_get_value(sym_codespace, opa->argv[idx].symb.text);
		  code->cptr = tmp.num;
		  if (code->cptr == 0) {
			struct cresolv_list_s*res = (struct cresolv_list_s*)
			      calloc(1, sizeof(struct cresolv_list_s));
			res->cp = code;
			res->lab = opa->argv[idx].symb.text;
			res->next = cresolv_list;
			cresolv_list = res;

		  } else {

			free(opa->argv[idx].symb.text);
		  }

		  break;

		case OA_FUNC_PTR:
		  if (opa->argv[idx].ltype != L_SYMB) {
			yyerror("operand format");
			break;
		  }

		  tmp = sym_get_value(sym_functors, opa->argv[idx].symb.text);
		  if (tmp.num == 0) {
			yyerror("functor undefined");
			break;
		  }
		  code->iptr = ipoint_index(tmp.num, opa->argv[idx].symb.idx);

		  free(opa->argv[idx].symb.text);
		  break;

		case OA_NUMBER:
		  if (opa->argv[idx].ltype != L_NUMB) {
			yyerror("operand format");
			break;
		  }

		  code->number = opa->argv[idx].numb;
		  break;

	    }
      }

      if (opa) free(opa);

      free(mnem);
}

void compile_codelabel(char*label)
{
      symbol_value_t val;
      vvp_cpoint_t ptr = codespace_next();

      val.num = ptr;
      sym_set_value(sym_codespace, label, val);

      free(label);
}

void compile_disable(char*label, struct symb_s symb)
{
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
      }


	/* Fill in the basics of the %disable in the instruction. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = of_DISABLE;

	/* Figure out the target SCOPE. */
      code->handle = compile_vpi_lookup(symb.text);
      assert(code->handle);

      free(label);
      free(symb.text);
}

/*
 * The %fork instruction is a little different from other instructions
 * in that it has an extended field that holds the information needed
 * to create the new thread. This includes the target PC and scope.
 * I get these from the parser in the form of symbols.
 */
void compile_fork(char*label, struct symb_s dest, struct symb_s scope)
{
      symbol_value_t tmp;
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
      }

	/* Fill in the basics of the %fork in the instruction. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = of_FORK;
      code->fork = new struct fork_extend;

	/* Figure out the target PC. */
      tmp = sym_get_value(sym_codespace, dest.text);
      code->fork->cptr = tmp.num;
      assert(code->fork->cptr);

	/* Figure out the target SCOPE. */
      vpiHandle sh = compile_vpi_lookup(scope.text);
      assert(sh);
      code->fork->scope = (struct __vpiScope*)sh;

      free(label);
      free(dest.text);
      free(scope.text);
}

void compile_vpi_call(char*label, char*name, unsigned argc, vpiHandle*argv)
{
      vvp_cpoint_t ptr = codespace_allocate();

	/* First, I can give the label a value that is the current
	   codespace pointer. Don't need the text of the label after
	   this is done. */
      if (label) {
	    symbol_value_t val;
	    val.num = ptr;
	    sym_set_value(sym_codespace, label, val);
	    free(label);
      }

	/* Create an instruction in the code space. */
      vvp_code_t code = codespace_index(ptr);
      code->opcode = &of_VPI_CALL;

	/* Create a vpiHandle that bundles the call information, and
	   store that handle in the instruction. */
      code->handle = vpip_build_vpi_call(name, argc, argv);
      if (code->handle == 0)
	    compile_errors += 1;

	/* Done with the lexor-allocated name string. */
      free(name);
}

/*
 * When the parser finds a thread statement, I create a new thread
 * with the start address referenced by the program symbol passed to
 * me.
 */
void compile_thread(char*start_sym)
{
      symbol_value_t tmp = sym_get_value(sym_codespace, start_sym);
      vvp_cpoint_t pc = tmp.num;
      if (pc == 0) {
	    yyerror("unresolved address");
	    return;
      }

      vthread_t thr = vthread_new(pc, vpip_peek_current_scope());
      schedule_vthread(thr, 0);
      free(start_sym);
}

vpiHandle compile_vpi_lookup(const char*label)
{
      symbol_value_t val;

      val = sym_get_value(sym_vpi, label);
      return (vpiHandle) val.ptr;
}

/*
 * A variable is a special functor, so we allocate that functor and
 * write the label into the symbol table.
 */
void compile_variable(char*label, char*name, int msb, int lsb,
		      bool signed_flag)
{
      unsigned wid = ((msb > lsb)? msb-lsb : lsb-msb) + 1;
      vvp_ipoint_t fdx = functor_allocate(wid);
      symbol_value_t val;
      val.num = fdx;
      sym_set_value(sym_functors, label, val);

      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	    functor_t obj = functor_index(ipoint_index(fdx,idx));
	    obj->table = ft_var;
	    obj->ival  = 0x22;
	    obj->oval  = 0x02;
	    obj->mode  = 0;
      }

	/* Make the vpiHandle for the reg. */
      vpiHandle obj = vpip_make_reg(name, msb, lsb, signed_flag, fdx);
      compile_vpi_symbol(label, obj);

      free(label);
}

void compile_net(char*label, char*name, int msb, int lsb, bool signed_flag,
		 unsigned argc, struct symb_s*argv)
{
      unsigned wid = ((msb > lsb)? msb-lsb : lsb-msb) + 1;
      vvp_ipoint_t fdx = functor_allocate(wid);
      symbol_value_t val;
      val.num = fdx;
      sym_set_value(sym_functors, label, val);

	/* Allocate all the functors for the net itself. */
      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	    functor_t obj = functor_index(ipoint_index(fdx,idx));
	    obj->table = ft_var;
	    obj->ival  = 0x22;
	    obj->oval  = 0x02;
	    obj->mode  = 0;
      }

      assert(argc == wid);

	/* Connect port[0] of each of the net functors to the output
	   of the addressed object. */
      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	    vvp_ipoint_t ptr = ipoint_index(fdx,idx);
	    functor_t obj = functor_index(ptr);

	    val = sym_get_value(sym_functors, argv[idx].text);
	    if (val.num) {

		  functor_t src = functor_index(ipoint_index(val.num,
							     argv[idx].idx));
		  obj->port[0] = src->out;
		  src->out = ptr;

	    } else {
		  postpone_functor_input(ipoint_make(ptr, 0),
					 argv[idx].text,
					 argv[idx].idx);
	    }
      }

	/* Make the vpiHandle for the reg. */
      vpiHandle obj = vpip_make_net(name, msb, lsb, signed_flag, fdx);
      compile_vpi_symbol(label, obj);

      free(label);
      free(argv);
}

/*
 * When parsing is otherwise complete, this function is called to do
 * the final stuff. Clean up deferred linking here.
 */
void compile_cleanup(void)
{
      struct resolv_list_s*tmp_list = resolv_list;
      resolv_list = 0;

      while (tmp_list) {
	    struct resolv_list_s*res = tmp_list;
	    tmp_list = res->next;

	      /* Get the addressed functor object and select the input
		 port that needs resolution. */
	    functor_t obj = functor_index(res->port);
	    unsigned idx = ipoint_port(res->port);

	      /* Try again to look up the symbol that was not defined
		 the first time around. */
	    symbol_value_t val = sym_get_value(sym_functors, res->source);
	    vvp_ipoint_t tmp = val.num;

	    if (tmp != 0) {
		    /* The symbol is defined, link the functor input
		       to the resolved output. */

		  tmp = ipoint_index(tmp, res->idx);
		  functor_t fport = functor_index(tmp);
		  obj->port[idx] = fport->out;
		  fport->out = res->port;

		  free(res->source);
		  free(res);

	    } else {
		    /* Still not resolved. put back into the list. */
		  res->next = resolv_list;
		  resolv_list = res;
	    }
      }

      struct cresolv_list_s*tmp_clist = cresolv_list;
      cresolv_list = 0;

      while (tmp_clist) {
	    struct cresolv_list_s*res = tmp_clist;
	    tmp_clist = res->next;

	    symbol_value_t val = sym_get_value(sym_codespace, res->lab);
	    vvp_cpoint_t tmp = val.num;

	    if (tmp != 0) {
		  res->cp->cptr = tmp;
		  free(res->lab);
		  
	    } else {
		  fprintf(stderr, "unresolved code label: %s\n", res->lab);
		  res->next = cresolv_list;
		  cresolv_list = res;
	    }
      }
}

void compile_dump(FILE*fd)
{
      fprintf(fd, "FUNCTOR SYMBOL TABLE:\n");
      sym_dump(sym_functors, fd);
      fprintf(fd, "FUNCTORS:\n");
      functor_dump(fd);
      fprintf(fd, "UNRESOLVED PORT INPUTS:\n");
      for (struct resolv_list_s*cur = resolv_list ;  cur ;  cur = cur->next)
	    fprintf(fd, "    %08x: %s\n", cur->port, cur->source);

      fprintf(fd, "CODE SPACE SYMBOL TABLE:\n");
      sym_dump(sym_codespace, fd);

      fprintf(fd, "CODE SPACE DISASSEMBLY:\n");
      codespace_dump(fd);
}

/*
 * $Log: compile.cc,v $
 * Revision 1.35  2001/04/18 04:21:23  steve
 *  Put threads into scopes.
 *
 * Revision 1.34  2001/04/15 16:37:48  steve
 *  add XOR support.
 *
 * Revision 1.33  2001/04/15 04:07:56  steve
 *  Add support for behavioral xnor.
 *
 * Revision 1.32  2001/04/14 05:10:56  steve
 *  support the .event/or statement.
 *
 * Revision 1.31  2001/04/13 03:55:18  steve
 *  More complete reap of all threads.
 *
 * Revision 1.30  2001/04/05 01:34:26  steve
 *  Add the .var/s and .net/s statements for VPI support.
 *
 * Revision 1.29  2001/04/05 01:12:28  steve
 *  Get signed compares working correctly in vvp.
 *
 * Revision 1.28  2001/04/01 22:25:33  steve
 *  Add the reduction nor instruction.
 *
 * Revision 1.27  2001/04/01 21:31:46  steve
 *  Add the buf functor type.
 *
 * Revision 1.26  2001/04/01 07:22:08  steve
 *  Implement the less-then and %or instructions.
 *
 * Revision 1.25  2001/04/01 06:40:45  steve
 *  Support empty statements for hanging labels.
 *
 * Revision 1.24  2001/04/01 06:12:13  steve
 *  Add the bitwise %and instruction.
 *
 * Revision 1.23  2001/04/01 04:34:28  steve
 *  Implement %cmp/x and %cmp/z instructions.
 *
 * Revision 1.22  2001/03/31 19:00:43  steve
 *  Add VPI support for the simulation time.
 *
 * Revision 1.21  2001/03/31 17:36:02  steve
 *  Add the jmp/1 instruction.
 *
 * Revision 1.20  2001/03/31 01:59:59  steve
 *  Add the ADD instrunction.
 *
 * Revision 1.19  2001/03/30 04:55:22  steve
 *  Add fork and join instructions.
 *
 * Revision 1.18  2001/03/29 03:46:36  steve
 *  Support named events as mode 2 functors.
 *
 * Revision 1.17  2001/03/28 17:24:32  steve
 *  include string.h for strcmp et al.
 *
 * Revision 1.16  2001/03/26 04:00:39  steve
 *  Add the .event statement and the %wait instruction.
 *
 * Revision 1.15  2001/03/25 19:38:23  steve
 *  Support NOR and NOT gates.
 *
 * Revision 1.14  2001/03/25 03:54:26  steve
 *  Add JMP0XZ and postpone net inputs when needed.
 *
 * Revision 1.13  2001/03/25 00:35:35  steve
 *  Add the .net statement.
 *
 * Revision 1.12  2001/03/23 02:40:22  steve
 *  Add the :module header statement.
 *
 * Revision 1.11  2001/03/22 22:38:13  steve
 *  Detect undefined system tasks at compile time.
 *
 * Revision 1.10  2001/03/22 05:28:41  steve
 *  Add code label forward references.
 *
 * Revision 1.9  2001/03/22 05:08:00  steve
 *  implement %load, %inv, %jum/0 and %cmp/u
 *
 * Revision 1.8  2001/03/21 05:13:03  steve
 *  Allow var objects as vpiHandle arguments to %vpi_call.
 *
 * Revision 1.7  2001/03/20 06:16:24  steve
 *  Add support for variable vectors.
 *
 * Revision 1.6  2001/03/18 04:35:18  steve
 *  Add support for string constants to VPI.
 *
 * Revision 1.5  2001/03/18 00:37:55  steve
 *  Add support for vpi scopes.
 *
 * Revision 1.4  2001/03/16 01:44:34  steve
 *  Add structures for VPI support, and all the %vpi_call
 *  instruction. Get linking of VPI modules to work.
 *
 * Revision 1.3  2001/03/11 23:06:49  steve
 *  Compact the vvp_code_s structure.
 *
 * Revision 1.2  2001/03/11 22:42:11  steve
 *  Functor values and propagation.
 *
 * Revision 1.1  2001/03/11 00:29:38  steve
 *  Add the vvp engine to cvs.
 *
 */

