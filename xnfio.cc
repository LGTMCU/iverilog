/*
 * Copyright (c) 1998 Stephen Williams (steve@icarus.com)
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
#ident "$Id: xnfio.cc,v 1.2 1999/07/17 22:01:14 steve Exp $"
#endif

# include  "functor.h"
# include  "netlist.h"

class xnfio_f  : public functor_t {

    public:
      void signal(Design*des, NetNet*sig);

    private:
};

static bool is_a_pad(const NetNet*net)
{
      if (net->attribute("PAD") == "")
	    return false;

      return true;
}

/*
 * The xnfio function looks for the PAD signals in the design, and
 * generates the needed IOB devices to handle being connected to the
 * actual FPGA PAD. This will add items to the netlist if needed.
 *
 * FIXME: If there is a DFF connected to the pad, try to convert it
 *        to an IO DFF instead. This would save a CLB, and it is
 *        really lame to not do the obvious optimization.
 */

static void make_obuf(Design*des, NetNet*net)
{
      assert(net->pin_count() == 1);

	/* FIXME: If there is nothing internally driving this PAD, I
	   can connect the PAD to a pullup and disconnect it from the
	   rest of the circuit. This would save routing resources. */
      assert(count_outputs(net->pin(0)) > 0);

	/* Look for an existing OBUF connected to this signal. If it
	   is there, then no need to add one. */
      for (NetObj::Link*idx = net->pin(0).next_link()
		 ; *idx != net->pin(0)  ; idx = idx->next_link()) {
	    NetLogic*tmp;
	    if ((tmp = dynamic_cast<NetLogic*>(idx->get_obj())) == 0)
		  continue;

	      // Try to use an existing BUF as an OBUF. This moves the
	      // BUF into the IOB.
	    if ((tmp->type() == NetLogic::BUF) &&
		(count_inputs(tmp->pin(0)) == 0) &&
		(count_outputs(tmp->pin(0)) == 1)) {
		  tmp->attribute("XNF-LCA", "OBUF:O,I");
		  return;
	    }

	      // Try to use an existing INV as an OBUF. Certain
	      // technologies support inverting the input of an OBUF,
	      // which looks just like an inverter. This uses the
	      // available resources of an IOB to optimize away an
	      // otherwise expensive inverter.
	    if ((tmp->type() == NetLogic::NOT) &&
		(count_inputs(tmp->pin(0)) == 0) &&
		(count_outputs(tmp->pin(0)) == 1)) {
		  tmp->attribute("XNF-LCA", "OBUF:O,~I");
		  return;
	    }
      }

	// Can't seem to find a way to rearrange the existing netlist,
	// so I am stuck creating a new buffer, the OBUF.
      NetLogic*buf = new NetLogic(des->local_symbol("$"), 2, NetLogic::BUF);
      des->add_node(buf);

      map<string,string>attr;
      attr["XNF-LCA"] = "OBUF:O,I";
      buf->set_attributes(attr);

	// Put the buffer between this signal and the rest of the
	// netlist.
      connect(net->pin(0), buf->pin(1));
      net->pin(0).unlink();
      connect(net->pin(0), buf->pin(0));

	// It is possible, in putting an OBUF between net and the rest
	// of the netlist, to create a ring without a signal. Detect
	// this case and create a new signal.
      if (count_signals(buf->pin(1)) == 0) {
	    NetNet*tmp = new NetNet(des->local_symbol("$"), NetNet::WIRE);
	    connect(buf->pin(1), tmp->pin(0));
	    des->add_signal(tmp);
      }
}

static void make_ibuf(Design*des, NetNet*net)
{
      assert(net->pin_count() == 1);
	// XXXX For now, require at least one input.
      assert(count_inputs(net->pin(0)) > 0);

	/* Look for an existing BUF connected to this signal and
	   suitably connected that I can use it as an IBUF. */
      for (NetObj::Link*idx = net->pin(0).next_link()
		 ; *idx != net->pin(0)  ; idx = idx->next_link()) {
	    NetLogic*tmp;
	    if ((tmp = dynamic_cast<NetLogic*>(idx->get_obj())) == 0)
		  continue;

	      // Found a BUF, it is only useable if the only input is
	      // the signal and there are no other inputs.
	    if ((tmp->type() == NetLogic::BUF) &&
		(count_inputs(tmp->pin(1)) == 1) &&
		(count_outputs(tmp->pin(1)) == 0)) {
		  tmp->attribute("XNF-LCA", "IBUF:O,I");
		  return;
	    }

      }

	// I give up, create an IBUF.
      NetLogic*buf = new NetLogic(des->local_symbol("$"), 2, NetLogic::BUF);
      des->add_node(buf);

      map<string,string>attr;
      attr["XNF-LCA"] = "IBUF:O,I";
      buf->set_attributes(attr);

	// Put the buffer between this signal and the rest of the
	// netlist.
      connect(net->pin(0), buf->pin(0));
      net->pin(0).unlink();
      connect(net->pin(0), buf->pin(1));

	// It is possible, in putting an OBUF between net and the rest
	// of the netlist, to create a ring without a signal. Detect
	// this case and create a new signal.
      if (count_signals(buf->pin(0)) == 0) {
	    NetNet*tmp = new NetNet(des->local_symbol("$"), NetNet::WIRE);
	    connect(buf->pin(0), tmp->pin(0));
	    des->add_signal(tmp);
      }
}

void xnfio_f::signal(Design*des, NetNet*net)
{
      if (! is_a_pad(net))
	    return;

      assert(net->pin_count() == 1);
      string pattr = net->attribute("PAD");

      switch (pattr[0]) {
	  case 'i':
	  case 'I':
	    make_ibuf(des, net);
	    break;
	  case 'o':
	  case 'O':
	    make_obuf(des, net);
	    break;
	      // FIXME: Only IPAD and OPAD supported. Need to
	      // add support for IOPAD.
	  default:
	    assert(0);
	    break;
      }
}

void xnfio(Design*des)
{
      xnfio_f xnfio_obj;
      des->functor(&xnfio_obj);
}

/*
 * $Log: xnfio.cc,v $
 * Revision 1.2  1999/07/17 22:01:14  steve
 *  Add the functor interface for functor transforms.
 *
 * Revision 1.1  1998/12/07 04:53:17  steve
 *  Generate OBUF or IBUF attributes (and the gates
 *  to garry them) where a wire is a pad. This involved
 *  figuring out enough of the netlist to know when such
 *  was needed, and to generate new gates and signales
 *  to handle what's missing.
 *
 */

