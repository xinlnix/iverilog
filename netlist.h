#ifndef __netlist_H
#define __netlist_H
/*
 * Copyright (c) 1998-1999 Stephen Williams (steve@icarus.com)
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
#ident "$Id: netlist.h,v 1.107 2000/01/10 01:35:24 steve Exp $"
#endif

/*
 * The netlist types, as described in this header file, are intended
 * to be the output from elaboration of the source design. The design
 * can be passed around in this form to the various stages and design
 * processors.
 */
# include  <string>
# include  <map>
# include  "verinum.h"
# include  "sref.h"
# include  "LineInfo.h"
# include  "svector.h"

class Design;
class NetNode;
class NetProc;
class NetProcTop;
class NetScope;
class NetExpr;
class NetESignal;
class ostream;


struct target;
struct functor_t;

/* =========
 * A NetObj is anything that has any kind of behavior in the
 * netlist. Nodes can be gates, registers, etc. and are linked
 * together to form a design web.
 *
 * The web of nodes that makes up a circuit is held together by the
 * Link class. There is a link for each pin. All mutually connected
 * pins form a ring of links.
 *
 * A link can be INPUT, OUTPUT or PASSIVE. An input never drives the
 * signal, and PASSIVE never receives the value of the signal. Wires
 * are PASSIVE, for example.
 *
 * A NetObj also has delays specified as rise_time, fall_time and
 * decay_time. The rise and fall time are the times to transition to 1
 * or 0 values. The decay_time is the time needed to decay to a 'bz
 * value, or to decay of the net is a trireg. The exact and precise
 * interpretation of the rise/fall/decay times is typically left to
 * the target to properly interpret.
 */
class NetObj {

    public:
      class Link {
	    friend void connect(Link&, Link&);
	    friend class NetObj;

	  public:
	    enum DIR { PASSIVE, INPUT, OUTPUT };
	    Link();
	    ~Link();

	      // Manipulate the link direction.
	    void set_dir(DIR d) { dir_ = d; }
	    DIR get_dir() const { return dir_; }

	    void cur_link(NetObj*&net, unsigned &pin)
		  { net = node_;
		    pin = pin_;
		  }

	    void next_link(NetObj*&net, unsigned&pin);
	    void next_link(const NetObj*&net, unsigned&pin) const;

	    Link* next_link();
	    const Link* next_link() const;

	      // Remove this link from the set of connected pins. The
	      // destructor will automatically do this if needed.
	    void unlink();

	      // Return true if this link is connected to anything else.
	    bool is_linked() const;

	      // Return true if these pins are connected.
	    bool is_linked(const NetObj::Link&that) const;

	      // Return true if this link is connected to any pin of r.
	    bool is_linked(const NetObj&r) const;

	    bool is_equal(const NetObj::Link&that) const
		  { return (node_ == that.node_) && (pin_ == that.pin_); }

	      // Return information about the object that this link is
	      // a part of.
	    const NetObj*get_obj() const;
	    NetObj*get_obj();
	    unsigned get_pin() const;

	    void set_name(const string&, unsigned inst =0);
	    const string& get_name() const;
	    unsigned get_inst() const;

	  private:
	      // The NetNode manages these. They point back to the
	      // NetNode so that following the links can get me here.
	    NetObj *node_;
	    unsigned pin_;
	    DIR dir_;

	      // These members name the pin of the link. If the name
	      // has width, then the ninst_ member is the index of the
	      // pin.
	    string   name_;
	    unsigned inst_;

	  private:
	    Link *next_;
	    Link *prev_;

	  private: // not implemented
	    Link(const Link&);
	    Link& operator= (const Link&);
      };

    public:
      explicit NetObj(const string&n, unsigned npins);
      virtual ~NetObj();

      const string& name() const { return name_; }

      unsigned pin_count() const { return npins_; }

      unsigned rise_time() const { return delay1_; }
      unsigned fall_time() const { return delay2_; }
      unsigned decay_time() const { return delay3_; }

      void rise_time(unsigned d) { delay1_ = d; }
      void fall_time(unsigned d) { delay2_ = d; }
      void decay_time(unsigned d) { delay3_ = d; }

      void set_attributes(const map<string,string>&);
      string attribute(const string&key) const;
      void attribute(const string&key, const string&value);

	// Return true if this has all the attributes in that and they
	// all have the same values.
      bool has_compat_attributes(const NetObj&that) const;

      bool test_mark() const { return mark_; }
      void set_mark(bool flag=true) { mark_ = flag; }

      Link&pin(unsigned idx);
      const Link&pin(unsigned idx) const;

      void dump_node_pins(ostream&, unsigned) const;
      void dump_obj_attr(ostream&, unsigned) const;

    private:
      string name_;
      Link*pins_;
      const unsigned npins_;
      unsigned delay1_;
      unsigned delay2_;
      unsigned delay3_;

      map<string,string> attributes_;

      bool mark_;
};

/*
 * A NetNode is a device of some sort, where each pin has a different
 * meaning. (i.e. pin(0) is the output to an and gate.) NetNode
 * objects are listed in the nodes_ of the Design object.
 */
class NetNode  : public NetObj {

    public:
      explicit NetNode(const string&n, unsigned npins)
      : NetObj(n, npins), node_next_(0), node_prev_(0), design_(0) { }

      virtual ~NetNode();

      virtual void emit_node(ostream&, struct target_t*) const;
      virtual void dump_node(ostream&, unsigned) const;

      virtual void functor_node(Design*, functor_t*);

    private:
      friend class Design;
      NetNode*node_next_, *node_prev_;
      Design*design_;
};


/*
 * NetNet is a special kind of NetObj that doesn't really do anything,
 * but carries the properties of the wire/reg/trireg. Thus, a set of
 * pins connected together would also be connected to exactly one of
 * these.
 *
 * Note that a net of any sort has exactly one pin. The pins feature
 * of the NetObj class is used to make a set of identical wires, in
 * order to support ranges, or busses. When dealing with vectors,
 * pin(0) is always the least significant bit.
 */
class NetNet  : public NetObj, public LineInfo {

    public:
      enum Type { IMPLICIT, IMPLICIT_REG, WIRE, TRI, TRI1, SUPPLY0,
		  WAND, TRIAND, TRI0, SUPPLY1, WOR, TRIOR, REG, INTEGER };

      enum PortType { NOT_A_PORT, PIMPLICIT, PINPUT, POUTPUT, PINOUT };

      explicit NetNet(NetScope*s, const string&n, Type t, unsigned npins =1);

      explicit NetNet(NetScope*s, const string&n, Type t, long ms, long ls);

      virtual ~NetNet();

      NetScope* scope();
      const NetScope* scope() const;

      Type type() const { return type_; }
      void type(Type t) { type_ = t; }

      PortType port_type() const { return port_type_; }
      void port_type(PortType t) { port_type_ = t; }

	/* These methods return the msb and lsb indices for the most
	   significant and least significant bits. These are signed
	   longs, and may be different from pin numbers. For example,
	   reg [1:8] has 8 bits, msb==1 and lsb==8. */
      long msb() const { return msb_; }
      long lsb() const { return lsb_; }

	/* This method converts a signed index (the type that might be
	   found in the verilog source) to a pin number. It accounts
	   for variation in the definition of the reg/wire/whatever. */
      unsigned sb_to_idx(long sb) const;

      bool local_flag() const { return local_flag_; }
      void local_flag(bool f) { local_flag_ = f; }

	/* NetESignal objects may reference this object. Keep a
	   reference count so that I keep track of them. */
      void incr_eref();
      void decr_eref();
      unsigned get_eref() const;

      verinum::V get_ival(unsigned pin) const
	    { return ivalue_[pin]; }
      void set_ival(unsigned pin, verinum::V val)
	    { ivalue_[pin] = val; }

      virtual void dump_net(ostream&, unsigned) const;

    private:
	// The Design class uses this for listing signals.
      friend class Design;
      NetNet*sig_next_, *sig_prev_;
      Design*design_;

    private:
      NetScope*scope_;
      Type   type_;
      PortType port_type_;

      long msb_, lsb_;

      bool local_flag_;
      unsigned eref_count_;

      verinum::V*ivalue_;
};

/*
 * This class implements the LPM_ADD_SUB component as described in the
 * EDIF LPM Version 2 1 0 standard. It is used as a structural
 * implementation of the + and - operators.
 */
class NetAddSub  : public NetNode {

    public:
      NetAddSub(const string&n, unsigned width);
      ~NetAddSub();

	// Get the width of the device (that is, the width of the
	// operands and results.)
      unsigned width() const;

      NetObj::Link& pin_Aclr();
      NetObj::Link& pin_Add_Sub();
      NetObj::Link& pin_Clock();
      NetObj::Link& pin_Cin();
      NetObj::Link& pin_Cout();
      NetObj::Link& pin_Overflow();

      NetObj::Link& pin_DataA(unsigned idx);
      NetObj::Link& pin_DataB(unsigned idx);
      NetObj::Link& pin_Result(unsigned idx);

      const NetObj::Link& pin_Cout() const;
      const NetObj::Link& pin_DataA(unsigned idx) const;
      const NetObj::Link& pin_DataB(unsigned idx) const;
      const NetObj::Link& pin_Result(unsigned idx) const;

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;
      virtual void functor_node(Design*des, functor_t*fun);
};

/*
 * This type represents the LPM_CLSHIFT device.
 */
class NetCLShift  : public NetNode {

    public:
      NetCLShift(const string&n, unsigned width, unsigned width_dist);
      ~NetCLShift();

      unsigned width() const;
      unsigned width_dist() const;

      NetObj::Link& pin_Direction();
      NetObj::Link& pin_Underflow();
      NetObj::Link& pin_Overflow();
      NetObj::Link& pin_Data(unsigned idx);
      NetObj::Link& pin_Result(unsigned idx);
      NetObj::Link& pin_Distance(unsigned idx);

      const NetObj::Link& pin_Direction() const;
      const NetObj::Link& pin_Underflow() const;
      const NetObj::Link& pin_Overflow() const;
      const NetObj::Link& pin_Data(unsigned idx) const;
      const NetObj::Link& pin_Result(unsigned idx) const;
      const NetObj::Link& pin_Distance(unsigned idx) const;

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;

    private:
      unsigned width_;
      unsigned width_dist_;
};

/*
 * This class supports the LPM_COMPARE device.
 *
 * NOTE: This is not the same as the device used to support case
 * compare. Case comparisons handle Vx and Vz values, whereas this
 * device need not.
 */
class NetCompare  : public NetNode {

    public:
      NetCompare(const string&n, unsigned width);
      ~NetCompare();

      unsigned width() const;

      NetObj::Link& pin_Aclr();
      NetObj::Link& pin_Clock();
      NetObj::Link& pin_AGB();
      NetObj::Link& pin_AGEB();
      NetObj::Link& pin_AEB();
      NetObj::Link& pin_ANEB();
      NetObj::Link& pin_ALB();
      NetObj::Link& pin_ALEB();

      NetObj::Link& pin_DataA(unsigned idx);
      NetObj::Link& pin_DataB(unsigned idx);

      const NetObj::Link& pin_Aclr() const;
      const NetObj::Link& pin_Clock() const;
      const NetObj::Link& pin_AGB() const;
      const NetObj::Link& pin_AGEB() const;
      const NetObj::Link& pin_AEB() const;
      const NetObj::Link& pin_ANEB() const;
      const NetObj::Link& pin_ALB() const;
      const NetObj::Link& pin_ALEB() const;

      const NetObj::Link& pin_DataA(unsigned idx) const;
      const NetObj::Link& pin_DataB(unsigned idx) const;

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;

    private:
      unsigned width_;
};

/*
 * This class represents an LPM_FF device. There is no literal gate
 * type in Verilog that maps, but gates of this type can be inferred.
 */
class NetFF  : public NetNode {

    public:
      NetFF(const string&n, unsigned width);
      ~NetFF();

      unsigned width() const;

      NetObj::Link& pin_Clock();
      NetObj::Link& pin_Enable();
      NetObj::Link& pin_Aload();
      NetObj::Link& pin_Aset();
      NetObj::Link& pin_Aclr();
      NetObj::Link& pin_Sload();
      NetObj::Link& pin_Sset();
      NetObj::Link& pin_Sclr();

      NetObj::Link& pin_Data(unsigned);
      NetObj::Link& pin_Q(unsigned);

      const NetObj::Link& pin_Clock() const;
      const NetObj::Link& pin_Enable() const;
      const NetObj::Link& pin_Data(unsigned) const;
      const NetObj::Link& pin_Q(unsigned) const;

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;
      virtual void functor_node(Design*des, functor_t*fun);
};


/*
 * This class represents the declared memory object. The parser
 * creates one of these for each declared memory in the elaborated
 * design. A reference to one of these is handled by the NetEMemory
 * object, which is derived from NetExpr. This is not a node because
 * memory objects can only be accessed by behavioral code.
 */
class NetMemory  {

    public:
      NetMemory(const string&n, long w, long s, long e);

      const string&name() const { return name_; }

	// This is the width (in bits) of a single memory position.
      unsigned width() const { return width_; }

	// This is the number of memory positions.
      unsigned count() const;

	// This method returns a 0 based address of a memory entry as
	// indexed by idx. The Verilog source may give index ranges
	// that are not zero based.
      unsigned index_to_address(long idx) const;

      void set_attributes(const map<string,string>&a);

      void dump(ostream&o, unsigned lm) const;

    private:
      string name_;
      unsigned width_;
      long idxh_;
      long idxl_;

      map<string,string> attributes_;

      friend class NetRamDq;
      NetRamDq* ram_list_;
};

/*
 * This class represents an LPM_MUX device. This device has some
 * number of Result points (the width of the device) and some number
 * of input choices. There is also a selector of some width. The
 * parameters are:
 *
 *      width  -- Width of the result and each possible Data input
 *      size   -- Number of Data input (each of width)
 *      selw   -- Width in bits of the select input
 */
class NetMux  : public NetNode {

    public:
      NetMux(const string&n, unsigned width, unsigned size, unsigned selw);
      ~NetMux();

      unsigned width() const;
      unsigned size() const;
      unsigned sel_width() const;

      NetObj::Link& pin_Aclr();
      NetObj::Link& pin_Clock();

      NetObj::Link& pin_Result(unsigned);
      NetObj::Link& pin_Data(unsigned wi, unsigned si);
      NetObj::Link& pin_Sel(unsigned);

      const NetObj::Link& pin_Aclr() const;
      const NetObj::Link& pin_Clock() const;

      const NetObj::Link& pin_Result(unsigned) const;
      const NetObj::Link& pin_Data(unsigned, unsigned) const;
      const NetObj::Link& pin_Sel(unsigned) const;

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;

    private:
      unsigned width_;
      unsigned size_;
      unsigned swidth_;
};

/*
 * This device represents an LPM_RAM_DQ device. The actual content is
 * represented by a NetMemory object allocated elsewhere, but that
 * object fixes the width and size of the device. The pin count of the
 * address input is given in the constructor.
 */
class NetRamDq  : public NetNode {

    public:
      NetRamDq(const string&name, NetMemory*mem, unsigned awid);
      ~NetRamDq();

      unsigned width() const;
      unsigned awidth() const;
      unsigned size() const;
      const NetMemory*mem() const;

      NetObj::Link& pin_InClock();
      NetObj::Link& pin_OutClock();
      NetObj::Link& pin_WE();

      NetObj::Link& pin_Address(unsigned idx);
      NetObj::Link& pin_Data(unsigned idx);
      NetObj::Link& pin_Q(unsigned idx);

      const NetObj::Link& pin_InClock() const;
      const NetObj::Link& pin_OutClock() const;
      const NetObj::Link& pin_WE() const;

      const NetObj::Link& pin_Address(unsigned idx) const;
      const NetObj::Link& pin_Data(unsigned idx) const;
      const NetObj::Link& pin_Q(unsigned idx) const;

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;

	// Use this method to absorb other NetRamDq objects that are
	// connected to the same memory, and have compatible pin
	// connections.
      void absorb_partners();

	// Use this method to count the partners (including myself)
	// that are ports to the attached memory.
      unsigned count_partners() const;

    private:
      NetMemory*mem_;
      NetRamDq*next_;
      unsigned awidth_;

};

/* =========
 * There are cases where expressions need to be represented. The
 * NetExpr class is the root of a heirarchy that serves that purpose.
 *
 * The expr_width() is the width of the expression, that accounts
 * for the widths of the sub-expressions I might have. It is up to the
 * derived classes to properly set the expr width, if need be. The
 * set_width() method is used to compel an expression to have a
 * certain width, and is used particulary when the expression is an
 * rvalue in an assignment statement.
 */
class NetExpr  : public LineInfo {
    public:
      explicit NetExpr(unsigned w =0);
      virtual ~NetExpr() =0;

      virtual void expr_scan(struct expr_scan_t*) const =0;
      virtual void dump(ostream&) const;

	// How wide am I?
      unsigned expr_width() const { return width_; }

	// Coerce the expression to have a specific width. If the
	// coersion works, then return true. Otherwise, return false.
      virtual bool set_width(unsigned);

	// This method evaluates the expression and returns an
	// equivilent expression that is reduced as far as compile
	// time knows how. Essentially, this is designed to fold
	// constants.
      virtual NetExpr*eval_tree();

	// Make a duplicate of myself, and subexpressions if I have
	// any. This is a deep copy operation.
      virtual NetExpr*dup_expr() const =0;

	// Return a version of myself that is structural. This is used
	// for converting expressions to gates.
      virtual NetNet*synthesize(Design*);


    protected:
      void expr_width(unsigned w) { width_ = w; }

    private:
      unsigned width_;

    private: // not implemented
      NetExpr(const NetExpr&);
      NetExpr& operator=(const NetExpr&);
};

/*
 * The expression constant is slightly special, and is sometimes
 * returned from other classes that can be evaluated at compile
 * time. This class represents constant values in expressions.
 */
class NetEConst  : public NetExpr {

    public:
      explicit NetEConst(const verinum&val);
      ~NetEConst();

      const verinum&value() const { return value_; }

      virtual bool set_width(unsigned w);
      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;

      virtual NetEConst* dup_expr() const;
      virtual NetNet*synthesize(Design*);

    private:
      verinum value_;
};

/*
 * The NetTmp object is a network that is only used momentarily by
 * elaboration to carry links around. A completed netlist should not
 * have any of these within. This is a kind of wire, so it is NetNet type.
 */
class NetTmp  : public NetNet {

    public:
      explicit NetTmp(const string&name, unsigned npins =1);

};

/*
 * The NetBUFZ is a magic device that represents the continuous
 * assign, with the output being the target register and the input
 * the logic that feeds it. The netlist preserves the directional
 * nature of that assignment with the BUFZ. The target may elide it if
 * that makes sense for the technology.
 */
class NetBUFZ  : public NetNode {

    public:
      explicit NetBUFZ(const string&n);
      ~NetBUFZ();

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;
};

/*
 * This node is used to represent case equality in combinational
 * logic. Although this is not normally synthesizeable, it makes sense
 * to support an abstract gate that can compare x and z.
 *
 * This pins are assigned as:
 *
 *     0   -- Output (always returns 0 or 1)
 *     1   -- Input
 *     2   -- Input
 */
class NetCaseCmp  : public NetNode {

    public:
      explicit NetCaseCmp(const string&n);
      ~NetCaseCmp();

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;
};

/*
 * This class represents instances of the LPM_CONSTANT device. The
 * node has only outputs and a constant value. The width is available
 * by getting the pin_count(), and the value bits are available one at
 * a time. There is no meaning to the aggregation of bits to form a
 * wide NetConst object, although some targets may have an easier time
 * detecting interesting constructs if they are combined.
 */
class NetConst  : public NetNode {

    public:
      explicit NetConst(const string&n, verinum::V v);
      explicit NetConst(const string&n, const verinum&val);
      ~NetConst();

      verinum::V value(unsigned idx) const;

      virtual void emit_node(ostream&, struct target_t*) const;
      virtual void functor_node(Design*, functor_t*);
      virtual void dump_node(ostream&, unsigned ind) const;

    private:
      verinum::V*value_;
};

/*
 * This class represents all manner of logic gates. Pin 0 is OUTPUT and
 * all the remaining pins are INPUT
 */
class NetLogic  : public NetNode {

    public:
      enum TYPE { AND, BUF, BUFIF0, BUFIF1, NAND, NOR, NOT, OR, XNOR,
		  XOR };

      explicit NetLogic(const string&n, unsigned pins, TYPE t);

      TYPE type() const { return type_; }

      virtual void dump_node(ostream&, unsigned ind) const;
      virtual void emit_node(ostream&, struct target_t*) const;
      virtual void functor_node(Design*, functor_t*);

    private:
      const TYPE type_;
};

/*
 * The UDP is a User Defined Primitive from the Verilog source. Do not
 * expand it out any further then this in the netlist, as this can be
 * used to represent target device primitives.
 *
 * The UDP can be combinational or sequential. The sequential UDP
 * includes the current output in the truth table, and supports edges,
 * whereas the combinational does not and is entirely level sensitive.
 * In any case, pin 0 is an output, and all the remaining pins are
 * inputs.
 *
 * The sequential truth table is canonically represented as a finite state
 * machine with the current state representing the inputs and the
 * current output, and the next state carrying the new output value to
 * use. All the outgoing transitions from a state represent a single
 * edge.
 *
 * Set_table takes as input a string with one letter per pin. The
 * parser translates the written sequences to one of these. The
 * valid characters are:
 *
 *      0, 1, x  -- The levels
 *      r   -- (01)
 *      R   -- (x1)
 *      f   -- (10)
 *      F   -- (x0)
 *      P   -- (0x)
 *      N   -- (1x)
 *
 * It also takes one of the following glob letters to represent more
 * then one item.
 *
 *      p   -- 01, 0x or x1
 *      n   -- 10, 1x or x0
 *      ?   -- 0, 1, or x
 *      *   -- any edge
 *      +   -- 01 or x1
 *      _   -- 10 or x0  (Note that this is not the output '-'.)
 *      %   -- 0x or 1x
 *
 * SEQUENTIAL
 * These objects have a single bit of memory. The logic table includes
 * an entry for the current value, and allows edges on the inputs. In
 * canonical form, inly then entries that generate 0, 1 or - (no change)
 * are listed.
 *
 * COMBINATIONAL
 * The logic table is a map between the input levels and the
 * output. Each input pin can have the value 0, 1 or x and the output
 * can have the values 0 or 1. If the input matches nothing, the
 * output is x. In canonical form, only the entries that generate 0 or
 * 1 are listed.
 *
 */
class NetUDP  : public NetNode {

    public:
      explicit NetUDP(const string&n, unsigned pins, bool sequ =false);

      virtual void emit_node(ostream&, struct target_t*) const;
      virtual void dump_node(ostream&, unsigned ind) const;

	/* return false if the entry conflicts with an existing
	   entry. In any case, the new output overrides. */
      bool set_table(const string&input, char output);
      void cleanup_table();

	/* Return the next output from the passed state. Each letter
	   of the input string represents the pin of the same
	   position. */
      char table_lookup(const string&from, char to, unsigned pin) const;

      void set_initial(char);
      char get_initial() const { return init_; }

      bool is_sequential() const { return sequential_; }

    private:
      bool sequential_;
      char init_;

      struct state_t_;
      struct pin_t_ {
	    state_t_*zer;
	    state_t_*one;
	    state_t_*xxx;

	    explicit pin_t_() : zer(0), one(0), xxx(0) { }
      };

      struct state_t_ {
	    char out;
	    pin_t_*pins;

	    state_t_(unsigned n) : out(0), pins(new pin_t_[n]) {}
	    ~state_t_() { delete[]pins; }
      };

      typedef map<string,state_t_*> FSM_;
      FSM_ fsm_;
      bool set_sequ_(const string&in, char out);
      bool sequ_glob_(string, char out);

      state_t_*find_state_(const string&);

	// A combinational primitive is more simply represented as a
	// simple map of input signals to a single output.
      typedef map<string,char> CM_;
      CM_ cm_;

      void dump_sequ_(ostream&o, unsigned ind) const;
      void dump_comb_(ostream&o, unsigned ind) const;
};

/* =========
 * A process is a behavioral-model description. A process is a
 * statement that may be compound. the various statement types may
 * refer to places in a netlist (by pointing to nodes) but is not
 * linked into the netlist. However, elaborating a process may cause
 * special nodes to be created to handle things like events.
 */
class NetProc  : public LineInfo {

    public:
      explicit NetProc();
      virtual ~NetProc();

	// This method is called to emit the statement to the
	// target. The target returns true if OK, false for errors.
      virtual bool emit_proc(ostream&, struct target_t*) const;

	// This method is called by functors that want to scan a
	// process in search of matchable patterns.
      virtual int match_proc(struct proc_match_t*);

      virtual void dump(ostream&, unsigned ind) const;

    private:
      friend class NetBlock;
      NetProc*next_;
};

/*
 * This is a procedural assignment. The lval is a register, and the
 * assignment happens when the code is executed by the design. The
 * node part of the NetAssign has as many pins as the width of the
 * lvalue object and represents the elaborated lvalue. Thus, this
 * appears as a procedural statement AND a structural node. The
 * LineInfo is the location of the assignment statement in the source.
 *
 * NOTE: The elaborator will make an effort to match the width of the
 * r-value to the with of the assign node, but targets and functions
 * should know that this is not a guarantee.
 */

class NetAssign_ : public NetProc, public NetNode {

    public:

	// This is the (procedural) value that is to be assigned when
	// the assignment is executed.
      NetExpr*rval();
      const NetExpr*rval() const;

	// If this expression exists, then only a single bit is to be
	// set from the rval, and the value of this expression selects
	// the pin that gets the value.
      const NetExpr*bmux() const;

    protected:
      NetAssign_(const string&n, unsigned w);
      virtual ~NetAssign_() =0;

      void set_rval(NetExpr*);
      void set_bmux(NetExpr*);

    private:
      NetExpr*rval_;
      NetExpr*bmux_;
};

class NetAssign  : public NetAssign_ {
    public:
      explicit NetAssign(const string&, Design*des, unsigned w, NetExpr*rv);
      explicit NetAssign(const string&, Design*des, unsigned w,
			 NetExpr*mux, NetExpr*rv);
      ~NetAssign();

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void emit_node(ostream&, struct target_t*) const;
      virtual int match_proc(struct proc_match_t*);
      virtual void dump(ostream&, unsigned ind) const;
      virtual void dump_node(ostream&, unsigned ind) const;

    private:
};

/*
 * ... and this is a non-blocking version of above.
 */
class NetAssignNB  : public NetAssign_ {
    public:
      explicit NetAssignNB(const string&, Design*des, unsigned w, NetExpr*rv);
      explicit NetAssignNB(const string&, Design*des, unsigned w,
			   NetExpr*mux, NetExpr*rv);
      ~NetAssignNB();


      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void emit_node(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;
      virtual void dump_node(ostream&, unsigned ind) const;

    private:
};

/*
 * Assignment to memory is handled separately because memory is
 * not a node. There are blocking and non-blocking variants, just like
 * regular assign, and the NetAssignMem_ base class takes care of all
 * the common stuff.
 */
class NetAssignMem_ : public NetProc {

    public:
      explicit NetAssignMem_(NetMemory*, NetNet*idx, NetExpr*rv);
      ~NetAssignMem_();

      NetMemory*memory() { return mem_; }
      NetNet*index()     { return index_; }
      NetExpr*rval()     { return rval_; }

      const NetMemory*memory()const { return mem_; }
      const NetNet*index()const     { return index_; }
      const NetExpr*rval()const     { return rval_; }

    private:
      NetMemory*mem_;
      NetNet * index_;
      NetExpr* rval_;
};

class NetAssignMem : public NetAssignMem_ {

    public:
      explicit NetAssignMem(NetMemory*, NetNet*idx, NetExpr*rv);
      ~NetAssignMem();

      virtual int match_proc(struct proc_match_t*);
      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:
};

class NetAssignMemNB : public NetAssignMem_ {

    public:
      explicit NetAssignMemNB(NetMemory*, NetNet*idx, NetExpr*rv);
      ~NetAssignMemNB();

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:
};

/* A block is stuff line begin-end blocks, that contain and ordered
   list of NetProc statements.

   NOTE: The emit method calls the target->proc_block function but
   does not recurse. It is up to the target-supplied proc_block
   function to call emit_recurse. */
class NetBlock  : public NetProc {

    public:
      enum Type { SEQU, PARA };

      NetBlock(Type t) : type_(t), last_(0) { }
      ~NetBlock();

      const Type type() const { return type_; }

      void append(NetProc*);

      void emit_recurse(ostream&, struct target_t*) const;
      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:
      const Type type_;

      NetProc*last_;
};

/*
 * A CASE statement in the verilog source leads, eventually, to one of
 * these. This is different from a simple conditional because of the
 * way the comparisons are performed. Also, it is likely that the
 * target may be able to optimize differently.
 *
 * Case cane be one of three types:
 *    EQ  -- All bits must exactly match
 *    EQZ -- z bits are don't care
 *    EQX -- x and z bits are don't care.
 */
class NetCase  : public NetProc {

    public:
      enum TYPE { EQ, EQX, EQZ };
      NetCase(TYPE c, NetExpr*ex, unsigned cnt);
      ~NetCase();

      void set_case(unsigned idx, NetExpr*ex, NetProc*st);

      TYPE type() const;
      const NetExpr*expr() const { return expr_; }
      unsigned nitems() const { return nitems_; }

      const NetExpr*expr(unsigned idx) const { return items_[idx].guard;}
      const NetProc*stat(unsigned idx) const { return items_[idx].statement; }

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:

      TYPE type_;

      struct Item {
	    NetExpr*guard;
	    NetProc*statement;
      };

      NetExpr* expr_;
      unsigned nitems_;
      Item*items_;
};


/* A condit represents a conditional. It has an expression to test,
   and a pair of statements to select from. */
class NetCondit  : public NetProc {

    public:
      explicit NetCondit(NetExpr*ex, NetProc*i, NetProc*e);
      ~NetCondit();

      const NetExpr*expr() const;
      NetExpr*expr();

      NetProc* if_clause();
      NetProc* else_clause();

      void emit_recurse_if(ostream&, struct target_t*) const;
      void emit_recurse_else(ostream&, struct target_t*) const;

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual int match_proc(struct proc_match_t*);
      virtual void dump(ostream&, unsigned ind) const;

    private:
      NetExpr* expr_;
      NetProc*if_;
      NetProc*else_;
};

/*
 * A forever statement is executed over and over again forever. Or
 * until its block is disabled.
 */
class NetForever : public NetProc {

    public:
      explicit NetForever(NetProc*s);
      ~NetForever();

      void emit_recurse(ostream&, struct target_t*) const;

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:
      NetProc*statement_;
};

/*
 * A funciton definition is elaborated just like a task, though by now
 * it is certain that the first parameter (a phantom parameter) is the
 * output and all the remaining parameters are the inputs. This makes
 * for easy code generation in targets that support behavioral descriptions.
 */
class NetFuncDef {

    public:
      NetFuncDef(const string&, const svector<NetNet*>&po);
      ~NetFuncDef();

      void set_proc(NetProc*st);

      const string& name() const;
      const NetProc*proc() const;

      unsigned port_count() const;
      const NetNet*port(unsigned idx) const;

      virtual void dump(ostream&, unsigned ind) const;

    private:
      string name_;
      NetProc*statement_;
      svector<NetNet*>ports_;
};

class NetPDelay  : public NetProc {

    public:
      NetPDelay(unsigned long d, NetProc*st)
      : delay_(d), statement_(st) { }

      unsigned long delay() const { return delay_; }

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

      void emit_proc_recurse(ostream&, struct target_t*) const;

    private:
      unsigned long delay_;
      NetProc*statement_;
};

/*
 * The NetPEvent is associated with NetNEvents. The NetPEvent receives
 * events from any one of the associated NetNEvents and in response
 * causes the attached statement to be executed. Objects of this type
 * are not nodes, but require a name anyhow so that backends can
 * generate objects to refer to it.
 *
 * The NetPEvent is the procedural part of the event.
 */
class NetNEvent;
class NetPEvent : public NetProc, public sref_back<NetPEvent,NetNEvent> {

    public:
      NetPEvent(const string&n, NetProc*st);
      ~NetPEvent();

      string name() const { return name_; }
      NetProc* statement();
      const NetProc* statement() const;

      virtual int match_proc(struct proc_match_t*);
      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

      void emit_proc_recurse(ostream&, struct target_t*) const;

    private:
      string name_;
      NetProc*statement_;
};

/*
 * The NetNEvent is a NetNode that connects to the structural part of
 * the design. It has only inputs, which cause the side effect of
 * triggering an event that the procedural part of the design can use.
 *
 * The NetNEvent may have wide input if is is an ANYEDGE type
 * device. This allows detecting changes in wide expressions.
 */
class NetNEvent  : public NetNode, public sref<NetPEvent,NetNEvent> {

    public:
      enum Type { ANYEDGE, POSEDGE, NEGEDGE, POSITIVE };

      NetNEvent(const string&ev, unsigned wid, Type e, NetPEvent*pe);
      ~NetNEvent();

      Type type() const { return edge_; }

      virtual void emit_node(ostream&, struct target_t*) const;

      void dump_proc(ostream&) const;
      virtual void dump_node(ostream&, unsigned ind) const;

    private:
      Type edge_;
};


/*
 * A repeat statement is executed some fixed number of times.
 */
class NetRepeat : public NetProc {

    public:
      explicit NetRepeat(NetExpr*e, NetProc*s);
      ~NetRepeat();

      const NetExpr*expr() const;
      void emit_recurse(ostream&, struct target_t*) const;

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:
      NetExpr*expr_;
      NetProc*statement_;
};

/*
 * The NetSTask class is a call to a system task. These kinds of tasks
 * are generally handled very simply in the target. They certainly are
 * handled differently from user defined tasks because ivl knows all
 * about the user defined tasks.
 */
class NetSTask  : public NetProc {

    public:
      NetSTask(const string&na, const svector<NetExpr*>&);
      ~NetSTask();

      const string& name() const { return name_; }

      unsigned nparms() const { return parms_.count(); }

      const NetExpr* parm(unsigned idx) const;

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:
      string name_;
      svector<NetExpr*>parms_;
};

/*
 * This class represents an elaborated class definition. NetUTask
 * classes may refer to objects of this type to get the meaning of the
 * defined task.
 */
class NetTaskDef {

    public:
      NetTaskDef(const string&n, const svector<NetNet*>&po);
      ~NetTaskDef();

      void set_proc(NetProc*p);

      const string& name() const { return name_; }
      const NetProc*proc() const { return proc_; }

      unsigned port_count() const { return ports_.count(); }
      NetNet*port(unsigned idx);

      void dump(ostream&, unsigned) const;

    private:
      string name_;
      NetProc*proc_;
      svector<NetNet*>ports_;

    private: // not implemented
      NetTaskDef(const NetTaskDef&);
      NetTaskDef& operator= (const NetTaskDef&);
};

/*
 * This node represents a function call in an expression. The object
 * contains a pointer to the function definition, which is used to
 * locate the value register and input expressions.
 *
 * The NetNet parameter to the constructor is the *register* NetNet
 * that receives the result of the function, and the NetExpr list is
 * the paraneters passed to the function.
 */
class NetEUFunc  : public NetExpr {

    public:
      NetEUFunc(NetFuncDef*, NetESignal*, svector<NetExpr*>&);
      ~NetEUFunc();

      const string& name() const;

      const NetESignal*result() const;
      unsigned parm_count() const;
      const NetExpr* parm(unsigned idx) const;

      const NetFuncDef* definition() const;

      virtual bool set_width(unsigned);
      virtual void dump(ostream&) const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual NetEUFunc*dup_expr() const;

    private:
      NetFuncDef*func_;
      NetESignal*result_;
      svector<NetExpr*> parms_;

    private: // not implemented
      NetEUFunc(const NetEUFunc&);
      NetEUFunc& operator= (const NetEUFunc&);
};

/*
 * A call to a user defined task is elaborated into this object. This
 * contains a pointer to the elaborated task definition, but is a
 * NetProc object so that it can be linked into statements.
 */
class NetUTask  : public NetProc {

    public:
      NetUTask(NetTaskDef*);
      ~NetUTask();

      const string& name() const { return task_->name(); }

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:
      NetTaskDef*task_;
};

/*
 * The while statement is a condition that is tested in the front of
 * each iteration, and a statement (a NetProc) that is executed as
 * long as the condition is true.
 */
class NetWhile  : public NetProc {

    public:
      NetWhile(NetExpr*c, NetProc*p)
      : cond_(c), proc_(p) { }

      const NetExpr*expr() const { return cond_; }

      void emit_proc_recurse(ostream&, struct target_t*) const;

      virtual bool emit_proc(ostream&, struct target_t*) const;
      virtual void dump(ostream&, unsigned ind) const;

    private:
      NetExpr* cond_;
      NetProc*proc_;
};


/*
 * The is the top of any process. It carries the type (initial or
 * always) and a pointer to the statement, probably a block, that
 * makes up the process.
 */
class NetProcTop  : public LineInfo {

    public:
      enum Type { KINITIAL, KALWAYS };

      NetProcTop(Type t, class NetProc*st);
      ~NetProcTop();

      Type type() const { return type_; }
      NetProc*statement();
      const NetProc*statement() const;

      void dump(ostream&, unsigned ind) const;
      bool emit(ostream&, struct target_t*tgt) const;

    private:
      const Type type_;
      NetProc*const statement_;

      friend class Design;
      NetProcTop*next_;
};

/*
 * This class represents a binary operator, with the left and right
 * operands and a single character for the operator. The operator
 * values are:
 *
 *   ^  -- Bit-wise exclusive OR
 *   +  -- Arithmetic add
 *   -  -- Arithmetic minus
 *   *  -- Arithmetic multiply
 *   /  -- Arithmetic divide
 *   %  -- Arithmetic modulus
 *   &  -- Bit-wise AND
 *   |  -- Bit-wise OR
 *   <  -- Less then
 *   >  -- Greater then
 *   e  -- Logical equality (==)
 *   E  -- Case equality (===)
 *   L  -- Less or equal
 *   G  -- Greater or equal
 *   n  -- Logical inequality (!=)
 *   N  -- Case inequality (!==)
 *   a  -- Logical AND (&&)
 *   o  -- Logical OR (||)
 *   O  -- Bit-wise NOR
 *   l  -- Left shift (<<)
 *   r  -- Right shift (>>)
 *   X  -- Bitwise exclusive NOR (~^)
 */
class NetEBinary  : public NetExpr {

    public:
      NetEBinary(char op, NetExpr*l, NetExpr*r);
      ~NetEBinary();

      const NetExpr*left() const { return left_; }
      const NetExpr*right() const { return right_; }

      char op() const { return op_; }

      virtual bool set_width(unsigned w);

      virtual NetEBinary* dup_expr() const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;

    protected:
      char op_;
      NetExpr* left_;
      NetExpr* right_;

      virtual void eval_sub_tree_();
};

/*
 * The addition operators have slightly more complex width
 * calculations because there is the optional carry bit that can be
 * used. The operators covered by this class are:
 *   +  -- Arithmetic add
 *   -  -- Arithmetic minus
 */
class NetEBAdd : public NetEBinary {

    public:
      NetEBAdd(char op, NetExpr*l, NetExpr*r);
      ~NetEBAdd();

      virtual bool set_width(unsigned w);
      virtual NetEBAdd* dup_expr() const;
      virtual NetEConst* eval_tree();
      virtual NetNet* synthesize(Design*);
};

/*
 * The bitwise binary operators are represented by this class. This is
 * a specialization of the binary operator, so is derived from
 * NetEBinary. The particular constraints on these operators are that
 * operand and result widths match exactly, and each bit slice of the
 * operation can be represented by a simple gate. The operators
 * covered by this class are:
 *
 *   ^  -- Bit-wise exclusive OR
 *   &  -- Bit-wise AND
 *   |  -- Bit-wise OR
 *   O  -- Bit-wise NOR
 *   X  -- Bit-wise XNOR (~^)
 */
class NetEBBits : public NetEBinary {

    public:
      NetEBBits(char op, NetExpr*l, NetExpr*r);
      ~NetEBBits();

      virtual bool set_width(unsigned w);
      virtual NetEBBits* dup_expr() const;

      virtual NetNet* synthesize(Design*);
};

/*
 * The binary comparison operators are handled by this class. This
 * this case the bit width of the expression is 1 bit, and the
 * operands take their natural widths. The supported operators are:
 *
 *   <  -- Less then
 *   >  -- Greater then
 *   e  -- Logical equality (==)
 *   E  -- Case equality (===)
 *   L  -- Less or equal (<=)
 *   G  -- Greater or equal (>=)
 *   n  -- Logical inequality (!=)
 *   N  -- Case inequality (!==)
 */
class NetEBComp : public NetEBinary {

    public:
      NetEBComp(char op, NetExpr*l, NetExpr*r);
      ~NetEBComp();

      virtual bool set_width(unsigned w);
      virtual NetEBComp* dup_expr() const;
      virtual NetEConst* eval_tree();

    private:
      NetEConst*eval_eqeq_();
      NetEConst*eval_leeq_();

};

/*
 * The binary logical operators are those that return boolean
 * results. The supported operators are:
 *
 *   a  -- Logical AND (&&)
 */
class NetEBLogic : public NetEBinary {

    public:
      NetEBLogic(char op, NetExpr*l, NetExpr*r);
      ~NetEBLogic();

      virtual bool set_width(unsigned w);
      virtual NetEBLogic* dup_expr() const;
      virtual NetEConst* eval_tree();

    private:
};


/*
 * The binary logical operators are those that return boolean
 * results. The supported operators are:
 *
 *   l  -- left shift (<<)
 *   r  -- right shift (>>)
 */
class NetEBShift : public NetEBinary {

    public:
      NetEBShift(char op, NetExpr*l, NetExpr*r);
      ~NetEBShift();

      virtual bool set_width(unsigned w);
      virtual NetEBShift* dup_expr() const;
      virtual NetEConst* eval_tree();

    private:
};


/*
 * This expression node supports the concat expression. This is an
 * operator that just glues the results of many expressions into a
 * single value.
 *
 * Note that the class stores the parameter expressions in source code
 * order. That is, the parm(0) is placed in the most significant
 * position of the result.
 */
class NetEConcat  : public NetExpr {

    public:
      NetEConcat(unsigned cnt, unsigned repeat =1);
      ~NetEConcat();

	// Manipulate the parameters.
      void set(unsigned idx, NetExpr*e);

      unsigned repeat() const { return repeat_; }
      unsigned nparms() const { return parms_.count() ; }
      NetExpr* parm(unsigned idx) const { return parms_[idx]; }

      virtual bool set_width(unsigned w);
      virtual NetEConcat* dup_expr() const;
      virtual NetExpr* eval_tree();
      virtual NetNet*synthesize(Design*);
      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;

    private:
      svector<NetExpr*>parms_;
      unsigned repeat_;
};

/*
 * This clas is a placeholder for a parameter expression. When
 * parameters are first created, an instance of this object is used to
 * hold the place where the parameter exression goes. Then, when the
 * parameters are resolved, these objects are removed.
 *
 * If the parameter object is created with a path and name, then the
 * object represents a reference to a parameter that is known to exist.
 */
class NetEParam  : public NetExpr {
    public:
      NetEParam();
      NetEParam(class Design*des, const string&path, const string&name);
      ~NetEParam();

      virtual bool set_width(unsigned w);
      virtual void expr_scan(struct expr_scan_t*) const;
      virtual NetExpr* eval_tree();
      virtual NetEParam* dup_expr() const;

      virtual void dump(ostream&) const;

    private:
      Design*des_;
      string path_;
      string name_;
};


/*
 * This class is a special (and magical) expression node type that
 * represents scope names. These can only be found as parameters to
 * NetSTask objects.
 */
class NetEScope  : public NetExpr {

    public:
      NetEScope(NetScope*);
      ~NetEScope();

      const NetScope* scope() const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual NetEScope* dup_expr() const;

      virtual void dump(ostream&os) const;

    private:
      NetScope*scope_;
};

/*
 * This node represents a system function call in an expression. The
 * object contains the name of the system function, which the backend
 * uses to to VPI matching.
 */
class NetESFunc  : public NetExpr {

    public:
      NetESFunc(const string&name, NetESignal*, svector<NetExpr*>&);
      ~NetESFunc();

      const string& name() const;

      const NetESignal*result() const;
      unsigned parm_count() const;
      const NetExpr* parm(unsigned idx) const;

      virtual bool set_width(unsigned);
      virtual void dump(ostream&) const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual NetESFunc*dup_expr() const;

    private:
      string name_;
      NetESignal*result_;
      svector<NetExpr*> parms_;

    private: // not implemented
      NetESFunc(const NetESFunc&);
      NetESFunc& operator= (const NetESFunc&);
};

/*
 * This class represents the ternary (?:) operator. It has 3
 * expressions, one of which is a condition used to select which of
 * the other two expressions is the result.
 */
class NetETernary  : public NetExpr {

    public:
      NetETernary(NetExpr*c, NetExpr*t, NetExpr*f);
      ~NetETernary();

      virtual bool set_width(unsigned w);

      const NetExpr*cond_expr() const;
      const NetExpr*true_expr() const;
      const NetExpr*false_expr() const;

      virtual NetETernary* dup_expr() const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;
      virtual NetNet*synthesize(Design*);

    private:
      NetExpr*cond_;
      NetExpr*true_val_;
      NetExpr*false_val_;
};

/*
 * This class represents a unary operator, with the single operand
 * and a single character for the operator. The operator values are:
 *
 *   ~  -- Bit-wise negation
 *   !  -- Logical negation
 *   &  -- Reduction AND
 *   |  -- Reduction OR
 *   ^  -- Reduction XOR
 *   +  --
 *   -  --
 *   A  -- Reduction NAND (~&)
 *   N  -- Reduction NOR (~|)
 *   X  -- Reduction NXOR (~^ or ^~)
 */
class NetEUnary  : public NetExpr {

    public:
      NetEUnary(char op, NetExpr*ex);
      ~NetEUnary();

      char op() const { return op_; }
      const NetExpr* expr() const { return expr_; }

      virtual bool set_width(unsigned w);

      virtual NetEUnary* dup_expr() const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;

    protected:
      char op_;
      NetExpr* expr_;
};

class NetEUBits : public NetEUnary {

    public:
      NetEUBits(char op, NetExpr*ex);
      ~NetEUBits();

      virtual NetNet* synthesize(Design*);

};

/* System identifiers are represented here. */
class NetEIdent  : public NetExpr {

    public:
      NetEIdent(const string&n, unsigned w)
      : NetExpr(w), name_(n) { }

      const string& name() const { return name_; }

      NetEIdent* dup_expr() const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;

    private:
      string name_;
};

/*
 * A reference to a memory is represented by this expression. If the
 * index is not supplied, then the node is only valid in certain
 * specific contexts.
 */
class NetEMemory  : public NetExpr {

    public:
      NetEMemory(NetMemory*mem, NetExpr*idx =0);
      virtual ~NetEMemory();

      const string& name () const { return mem_->name(); }
      const NetExpr* index() const { return idx_; }

      virtual bool set_width(unsigned);

      virtual NetEMemory*dup_expr() const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;

    private:
      NetMemory*mem_;
      NetExpr* idx_;
};

/*
 * When a signal shows up in an expression, this type represents
 * it. From this the expression can get any kind of access to the
 * structural signal.
 *
 * A signal shows up as a node in the netlist so that structural
 * activity can invoke the expression.
 */
class NetESignal  : public NetExpr {

    public:
      NetESignal(NetNet*n);
      ~NetESignal();

      const string& name() const;
      virtual bool set_width(unsigned);

      virtual NetESignal* dup_expr() const;
      NetNet* synthesize(Design*des);

	// These methods actually reference the properties of the
	// NetNet object that I point to.
      unsigned pin_count() const;
      NetObj::Link& pin(unsigned idx);

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;

    private:
      NetNet*net_;
};

/*
 * An expression that takes a portion of a signal is represented as
 * one of these. For example, ``foo[x+5]'' is a signal and x+5 is an
 * expression to select a single bit from that signal. I can't just
 * make a new NetESignal node connected to the single net because the
 * expression may vary during execution, so the structure is not known
 * at compile (elaboration) time.
 */
class NetESubSignal  : public NetExpr {

    public:
      NetESubSignal(NetESignal*sig, NetExpr*ex);
      ~NetESubSignal();

      const string&name() const { return sig_->name(); }
      const NetExpr*index() const { return idx_; }

      virtual bool set_width(unsigned);

      NetESubSignal* dup_expr() const;

      virtual void expr_scan(struct expr_scan_t*) const;
      virtual void dump(ostream&) const;

    private:
	// For now, only support single-bit selects of a signal.
      NetESignal*sig_;
      NetExpr* idx_;
};


/*
 * This object type is used to contain a logical scope within a
 * design. The scope doesn't represent any executable hardware, but is
 * just a handle that netlist processors can use to grab at the design.
 */
class NetScope {

    public:
      enum TYPE { MODULE, BEGIN_END, FORK_JOIN };
      NetScope(const string&root);
      NetScope(NetScope*up, const string&name, TYPE t);
      ~NetScope();

      TYPE type() const;
      string name() const;
      const NetScope* parent() const;

      void dump(ostream&) const;

    private:
      TYPE type_;
      string name_;
      NetScope*up_;
};

/*
 * This class contains an entire design. It includes processes and a
 * netlist, and can be passed around from function to function.
 */
class Design {

    public:
      Design();
      ~Design();


	/* The flags are a generic way of accepting command line
	   parameters/flags and passing them to the processing steps
	   that deal with the design. The compilation driver sets the
	   entire flags map after elaboration is done. Subsequent
	   steps can then use the get_flag() function to get the value
	   of an interesting key. */

      void set_flags(const map<string,string>&f) { flags_ = f; }

      string get_flag(const string&key) const;

      NetScope* make_root_scope(const string&name);
      NetScope* make_scope(const string&path, NetScope::TYPE t,
			   const string&name);
      NetScope* find_scope(const string&path);

	// PARAMETERS
      void set_parameter(const string&, NetExpr*);
      const NetExpr*find_parameter(const string&path, const string&name) const;

	// SIGNALS
      void add_signal(NetNet*);
      void del_signal(NetNet*);
      NetNet*find_signal(const string&path, const string&name);

	// Memories
      void add_memory(NetMemory*);
      NetMemory* find_memory(const string&path, const string&name);

	// Functions
      void add_function(const string&n, NetFuncDef*);
      NetFuncDef* find_function(const string&path, const string&key);
      NetFuncDef* find_function(const string&path);

	// Tasks
      void add_task(const string&n, NetTaskDef*);
      NetTaskDef* find_task(const string&path, const string&name);
      NetTaskDef* find_task(const string&key);

	// NODES
      void add_node(NetNode*);
      void del_node(NetNode*);

	// PROCESSES
      void add_process(NetProcTop*);
      void delete_process(NetProcTop*);

	// Iterate over the design...
      void dump(ostream&) const;
      void functor(struct functor_t*);
      bool emit(ostream&, struct target_t*) const;

      void clear_node_marks();
      NetNode*find_node(bool (*test)(const NetNode*));

      void clear_signal_marks();
      NetNet*find_signal(bool (*test)(const NetNet*));

	// This is incremented by elaboration when an error is
	// detected. It prevents code being emitted.
      unsigned errors;

    public:
      string local_symbol(const string&path);

    private:
      map<string,NetScope*> scopes_;

	// List all the parameters in the design. This table includes
	// the parameters of instantiated modules in canonical names.
      map<string,NetExpr*> parameters_;

	// List all the signals in the design.
      NetNet*signals_;

      map<string,NetMemory*> memories_;

	// List the function definitions in the design.
      map<string,NetFuncDef*> funcs_;

	// List the task definitions in the design.
      map<string,NetTaskDef*> tasks_;

	// List the nodes in the design
      NetNode*nodes_;

	// List the processes in the design.
      NetProcTop*procs_;
      NetProcTop*procs_idx_;

      map<string,string> flags_;

      unsigned lcounter_;

    private: // not implemented
      Design(const Design&);
      Design& operator= (const Design&);
};


/* =======
 */

inline bool operator == (const NetObj::Link&l, const NetObj::Link&r)
{ return l.is_equal(r); }

inline bool operator != (const NetObj::Link&l, const NetObj::Link&r)
{ return ! l.is_equal(r); }

/* Connect the pins of two nodes together. Either may already be
   connected to other things, connect is transitive. */
extern void connect(NetObj::Link&, NetObj::Link&);

/* Return true if l and r are connected. */
inline bool connected(const NetObj::Link&l, const NetObj::Link&r)
{ return l.is_linked(r); }

/* Return true if l is fully connected to r. This means, every pin in
   l is connected to a pin in r. This is expecially useful for
   checking signal vectors. */
extern bool connected(const NetObj&l, const NetObj&r);

/* return the number of links in the ring that are of the specified
   type. */
extern unsigned count_inputs(const NetObj::Link&pin);
extern unsigned count_outputs(const NetObj::Link&pin);
extern unsigned count_signals(const NetObj::Link&pin);

/* Find the next link that is an output into the nexus. */
extern NetObj::Link* find_next_output(NetObj::Link*lnk);

/* Find the signal connected to the given node pin. There should
   always be exactly one signal. The bidx parameter get filled with
   the signal index of the Net, in case it is a vector. */
const NetNet* find_link_signal(const NetObj*net, unsigned pin,
			       unsigned&bidx);

inline ostream& operator << (ostream&o, const NetExpr&exp)
{ exp.dump(o); return o; }

extern ostream& operator << (ostream&, NetNet::Type);

/*
 * $Log: netlist.h,v $
 * Revision 1.107  2000/01/10 01:35:24  steve
 *  Elaborate parameters afer binding of overrides.
 *
 * Revision 1.106  2000/01/01 06:18:00  steve
 *  Handle synthesis of concatenation.
 *
 * Revision 1.105  1999/12/30 04:19:12  steve
 *  Propogate constant 0 in low bits of adders.
 *
 * Revision 1.104  1999/12/17 06:18:16  steve
 *  Rewrite the cprop functor to use the functor_t interface.
 *
 * Revision 1.103  1999/12/17 03:38:46  steve
 *  NetConst can now hold wide constants.
 *
 * Revision 1.102  1999/12/16 02:42:15  steve
 *  Simulate carry output on adders.
 *
 * Revision 1.101  1999/12/12 06:03:14  steve
 *  Allow memories without indices in expressions.
 *
 * Revision 1.100  1999/12/09 06:00:00  steve
 *  Fix const/non-const errors.
 *
 * Revision 1.99  1999/12/05 19:30:43  steve
 *  Generate XNF RAMS from synthesized memories.
 *
 * Revision 1.98  1999/12/05 02:24:09  steve
 *  Synthesize LPM_RAM_DQ for writes into memories.
 *
 * Revision 1.97  1999/12/01 06:06:16  steve
 *  Redo synth to use match_proc_t scanner.
 *
 * Revision 1.96  1999/11/28 23:42:02  steve
 *  NetESignal object no longer need to be NetNode
 *  objects. Let them keep a pointer to NetNet objects.
 *
 * Revision 1.95  1999/11/27 19:07:58  steve
 *  Support the creation of scopes.
 *
 * Revision 1.94  1999/11/24 04:01:59  steve
 *  Detect and list scope names.
 *
 * Revision 1.93  1999/11/21 17:35:37  steve
 *  Memory name lookup handles scopes.
 *
 * Revision 1.92  1999/11/21 00:13:09  steve
 *  Support memories in continuous assignments.
 *
 * Revision 1.91  1999/11/19 05:02:37  steve
 *  handle duplicate connect to a nexus.
 *
 * Revision 1.90  1999/11/19 03:02:25  steve
 *  Detect flip-flops connected to opads and turn
 *  them into OUTFF devices. Inprove support for
 *  the XNF-LCA attribute in the process.
 *
 * Revision 1.89  1999/11/18 03:52:19  steve
 *  Turn NetTmp objects into normal local NetNet objects,
 *  and add the nodangle functor to clean up the local
 *  symbols generated by elaboration and other steps.
 *
 * Revision 1.88  1999/11/14 23:43:45  steve
 *  Support combinatorial comparators.
 *
 * Revision 1.87  1999/11/14 20:24:28  steve
 *  Add support for the LPM_CLSHIFT device.
 *
 * Revision 1.86  1999/11/05 04:40:40  steve
 *  Patch to synthesize LPM_ADD_SUB from expressions,
 *  Thanks to Larry Doolittle. Also handle constants
 *  in expressions.
 *
 *  Synthesize adders in XNF, based on a patch from
 *  Larry. Accept synthesis of constants from Larry
 *  as is.
 *
 * Revision 1.85  1999/11/04 03:53:26  steve
 *  Patch to synthesize unary ~ and the ternary operator.
 *  Thanks to Larry Doolittle <LRDoolittle@lbl.gov>.
 *
 *  Add the LPM_MUX device, and integrate it with the
 *  ternary synthesis from Larry. Replace the lpm_mux
 *  generator in t-xnf.cc to use XNF EQU devices to
 *  put muxs into function units.
 *
 *  Rewrite elaborate_net for the PETernary class to
 *  also use the LPM_MUX device.
 *
 * Revision 1.84  1999/11/04 01:12:42  steve
 *  Elaborate combinational UDP devices.
 *
 * Revision 1.83  1999/11/02 04:55:34  steve
 *  Add the synthesize method to NetExpr to handle
 *  synthesis of expressions, and use that method
 *  to improve r-value handling of LPM_FF synthesis.
 *
 *  Modify the XNF target to handle LPM_FF objects.
 *
 * Revision 1.82  1999/11/01 02:07:40  steve
 *  Add the synth functor to do generic synthesis
 *  and add the LPM_FF device to handle rows of
 *  flip-flops.
 *
 * Revision 1.81  1999/10/31 04:11:27  steve
 *  Add to netlist links pin name and instance number,
 *  and arrange in vvm for pin connections by name
 *  and instance number.
 *
 * Revision 1.80  1999/10/10 23:29:37  steve
 *  Support evaluating + operator at compile time.
 *
 * Revision 1.79  1999/10/10 01:59:55  steve
 *  Structural case equals device.
 *
 * Revision 1.78  1999/10/07 05:25:34  steve
 *  Add non-const bit select in l-value of assignment.
 *
 * Revision 1.77  1999/10/06 05:06:16  steve
 *  Move the rvalue into NetAssign_ common code.
 *
 * Revision 1.76  1999/09/30 21:28:34  steve
 *  Handle mutual reference of tasks by elaborating
 *  task definitions in two passes, like functions.
 *
 * Revision 1.75  1999/09/30 02:43:02  steve
 *  Elaborate ~^ and ~| operators.
 *
 * Revision 1.74  1999/09/29 18:36:03  steve
 *  Full case support
 *
 * Revision 1.73  1999/09/28 03:11:30  steve
 *  Get the bit widths of unary operators that return one bit.
 *
 * Revision 1.72  1999/09/25 02:57:30  steve
 *  Parse system function calls.
 *
 * Revision 1.71  1999/09/23 03:56:57  steve
 *  Support shift operators.
 *
 * Revision 1.70  1999/09/23 00:21:55  steve
 *  Move set_width methods into a single file,
 *  Add the NetEBLogic class for logic expressions,
 *  Fix error setting with of && in if statements.
 *
 * Revision 1.69  1999/09/22 16:57:23  steve
 *  Catch parallel blocks in vvm emit.
 *
 * Revision 1.68  1999/09/21 00:13:40  steve
 *  Support parameters that reference other paramters.
 *
 * Revision 1.67  1999/09/20 02:21:10  steve
 *  Elaborate parameters in phases.
 *
 * Revision 1.66  1999/09/18 01:53:08  steve
 *  Detect constant lessthen-equal expressions.
 *
 * Revision 1.65  1999/09/16 04:18:15  steve
 *  elaborate concatenation repeats.
 *
 * Revision 1.64  1999/09/15 01:55:06  steve
 *  Elaborate non-blocking assignment to memories.
 *
 * Revision 1.63  1999/09/13 03:10:59  steve
 *  Clarify msb/lsb in context of netlist. Properly
 *  handle part selects in lval and rval of expressions,
 *  and document where the least significant bit goes
 *  in NetNet objects.
 *
 * Revision 1.62  1999/09/11 04:43:17  steve
 *  Support ternary and <= operators in vvm.
 *
 * Revision 1.61  1999/09/08 04:05:30  steve
 *  Allow assign to not match rvalue width.
 *
 * Revision 1.60  1999/09/03 04:28:38  steve
 *  elaborate the binary plus operator.
 *
 * Revision 1.59  1999/09/01 20:46:19  steve
 *  Handle recursive functions and arbitrary function
 *  references to other functions, properly pass
 *  function parameters and save function results.
 *
 * Revision 1.58  1999/08/31 22:38:29  steve
 *  Elaborate and emit to vvm procedural functions.
 *
 * Revision 1.57  1999/08/25 22:22:41  steve
 *  elaborate some aspects of functions.
 *
 * Revision 1.56  1999/08/18 04:00:02  steve
 *  Fixup spelling and some error messages. <LRDoolittle@lbl.gov>
 *
 * Revision 1.55  1999/08/06 04:05:28  steve
 *  Handle scope of parameters.
 *
 * Revision 1.54  1999/08/01 21:48:11  steve
 *  set width of procedural r-values when then
 *  l-value is a memory word.
 *
 * Revision 1.53  1999/08/01 16:34:50  steve
 *  Parse and elaborate rise/fall/decay times
 *  for gates, and handle the rules for partial
 *  lists of times.
 *
 * Revision 1.52  1999/07/31 03:16:54  steve
 *  move binary operators to derived classes.
 *
 * Revision 1.51  1999/07/24 02:11:20  steve
 *  Elaborate task input ports.
 *
 * Revision 1.50  1999/07/18 21:17:50  steve
 *  Add support for CE input to XNF DFF, and do
 *  complete cleanup of replaced design nodes.
 *
 * Revision 1.49  1999/07/18 05:52:47  steve
 *  xnfsyn generates DFF objects for XNF output, and
 *  properly rewrites the Design netlist in the process.
 *
 * Revision 1.48  1999/07/17 22:01:13  steve
 *  Add the functor interface for functor transforms.
 *
 * Revision 1.47  1999/07/17 19:51:00  steve
 *  netlist support for ternary operator.
 *
 * Revision 1.46  1999/07/17 03:08:32  steve
 *  part select in expressions.
 *
 * Revision 1.45  1999/07/16 04:33:41  steve
 *  set_width for NetESubSignal.
 *
 * Revision 1.44  1999/07/07 04:20:57  steve
 *  Emit vvm for user defined tasks.
 *
 * Revision 1.43  1999/07/03 02:12:51  steve
 *  Elaborate user defined tasks.
 *
 * Revision 1.42  1999/06/24 04:24:18  steve
 *  Handle expression widths for EEE and NEE operators,
 *  add named blocks and scope handling,
 *  add registers declared in named blocks.
 *
 * Revision 1.41  1999/06/19 21:06:16  steve
 *  Elaborate and supprort to vvm the forever
 *  and repeat statements.
 *
 * Revision 1.40  1999/06/13 23:51:16  steve
 *  l-value part select for procedural assignments.
 *
 * Revision 1.39  1999/06/13 17:30:23  steve
 *  More unary operators.
 *
 * Revision 1.38  1999/06/13 16:30:06  steve
 *  Unify the NetAssign constructors a bit.
 *
 * Revision 1.37  1999/06/09 03:00:06  steve
 *  Add support for procedural concatenation expression.
 *
 * Revision 1.36  1999/06/06 20:45:38  steve
 *  Add parse and elaboration of non-blocking assignments,
 *  Replace list<PCase::Item*> with an svector version,
 *  Add integer support.
 *
 * Revision 1.35  1999/06/03 05:16:25  steve
 *  Compile time evalutation of constant expressions.
 *
 * Revision 1.34  1999/06/02 15:38:46  steve
 *  Line information with nets.
 *
 * Revision 1.33  1999/05/30 01:11:46  steve
 *  Exressions are trees that can duplicate, and not DAGS.
 *
 * Revision 1.32  1999/05/27 04:13:08  steve
 *  Handle expression bit widths with non-fatal errors.
 *
 * Revision 1.31  1999/05/16 05:08:42  steve
 *  Redo constant expression detection to happen
 *  after parsing.
 *
 *  Parse more operators and expressions.
 *
 * Revision 1.30  1999/05/12 04:03:19  steve
 *  emit NetAssignMem objects in vvm target.
 *
 * Revision 1.29  1999/05/10 00:16:58  steve
 *  Parse and elaborate the concatenate operator
 *  in structural contexts, Replace vector<PExpr*>
 *  and list<PExpr*> with svector<PExpr*>, evaluate
 *  constant expressions with parameters, handle
 *  memories as lvalues.
 *
 *  Parse task declarations, integer types.
 */
#endif
