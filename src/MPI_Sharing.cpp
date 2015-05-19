#include "MPI_Sharing.h"
#include "IRMutator.h"
#include "IR.h"
#include "Bounds.h"

#include <iostream>
#include <string>

namespace Halide {
    namespace Internal {

	using std::string;
	using std::vector;

	// Visitor and helper function to test if a piece of IR uses an extern image.
	class ReplaceExternImage : public IRMutator {
	    using IRMutator::visit;
        
	    Buffer replacement_image;

	    void visit(const Call *c) {
		if (c->call_type == Call::Image) {
		    expr = Call::make(replacement_image.type(), replacement_image.name(), c->args);
		} else {
		    IRMutator::visit(c);
		}
	    }
	public:
	    ReplaceExternImage(Buffer replacement_image) : replacement_image(replacement_image) {}
	};

	class MPI_Sharing : public IRMutator {
	public:
	    using IRMutator::visit;
	    void visit(const MPI_Share *op) {
		Buffer buf = op->image;
		string buf_name = buf.name();

		string collected_name = buf_name + "_collected";

		/*Expr null_handle = Call::make(Handle(), Call::null_handle,
		  vector<Expr>(), Call::Intrinsic);
            
		  // referencing the code in BoundsInference
		  Expr accum_stride = IntImm::make(1);
		  for (int i = 0; i < buf.dimensions(); i++) {
		  Expr tmin = op->touched[i].min;
		  Expr tmax = op->touched[i].max;
		  Expr text = tmax - tmin;
		  collected_args.push_back(tmin);
		  collected_args.push_back(text);
		  accum_stride *= text;
		  extents.push_back(text);
		  }
		  Expr collected_buffer_t = Call::make(Handle(), Call::create_buffer_t,
		  collected_args, Call::Intrinsic);
		  //stmt = LetStmt::make(collected_name + ".buffer", collected_buffer_t, op->body);
		  stmt = Allocate::make(collected_name, buf.type(), extents, const_true(), op->body);
		*/

		//actually, let's try letting realize do the hard work
		Region collected_region;
		for (int i = 0; i < buf.dimensions(); i++) {
		    Expr tmin = op->touched[i].min;
		    Expr tmax = op->touched[i].max;
		    Expr text = tmax - tmin;
		    collected_region.push_back(Range(tmin, text));
		}
		vector<Type> types(1);
		types[0] = buf.type();

		vector<Expr> collect_args;
		Expr mpi_collect_eval =  Call::make(Bool(), Call::mpi_collect,
						     collect_args, Call::Intrinsic);

		stmt = op->body;
		stmt = Block::make(Evaluate::make(mpi_collect_eval), stmt);
		stmt = Realize::make(collected_name, types, collected_region, const_true(), stmt);
		    

		Buffer replacement_image(buf.type(), buf.raw_buffer(), collected_name);
		ReplaceExternImage replacer(replacement_image);
		stmt = replacer.mutate(stmt);
	    }
	};

	Stmt mpi_sharing(Stmt s) {
	    // use blocks and evaluate/call to insert ext calls
	    s = MPI_Sharing().mutate(s);
	    return s;
	}
    }
}
