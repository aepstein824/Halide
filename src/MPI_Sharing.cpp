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

    class MPI_Sharing : public IRMutator {
    public:
	using IRMutator::visit;
	void visit(const MPI_Share *op) {
	    Buffer buf = op->image;
	    string buf_name = buf.name();

	    string collected_name = buf_name + "_collected";

            Expr null_handle = Call::make(Handle(), Call::null_handle, vector<Expr>(), Call::Intrinsic);
	    
	    // referencing the code in BoundsInference
	    vector<Expr> collected_args;
	    vector<Expr> extents;
	    collected_args.push_back(null_handle);
	    collected_args.push_back(buf.type().bytes());
	    Expr accum_stride = IntImm::make(1);
	    for (int i = 0; i < buf.dimensions(); i++) {
		Expr tmin = op->touched[i].min;
		Expr tmax = op->touched[i].max;
		Expr text = tmax - tmin;
		collected_args.push_back(tmin);
		collected_args.push_back(text);
		collected_args.push_back(accum_stride);
		accum_stride *= text;
		extents.push_back(text);
	    }
	    Expr collected_buffer_t = Call::make(Handle(), Call::create_buffer_t, collected_args,
					      Call::Intrinsic);
	    stmt = LetStmt::make(collected_name + ".buffer", collected_buffer_t, op->body);
	    stmt = Allocate::make(collected_name, buf.type(), extents, const_true(), stmt);
	}
    };

    Stmt mpi_sharing(Stmt s) {
	// use blocks and evaluate/call to insert ext calls
	s = MPI_Sharing().mutate(s);
	return s;
    }
}
}
