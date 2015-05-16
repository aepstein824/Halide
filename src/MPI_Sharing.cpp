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
                std::cout << "Replacing image call: " << c->name << " -> "
                          << replacement_image.name() << "\n";
                expr = Call::make(replacement_image, c->args);
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

            Expr null_handle = Call::make(Handle(), Call::null_handle,
                                          vector<Expr>(), Call::Intrinsic);
            
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
	    Expr collected_buffer_t = Call::make(Handle(), Call::create_buffer_t,
                                                 collected_args, Call::Intrinsic);
	    //stmt = LetStmt::make(collected_name + ".buffer", collected_buffer_t, op->body);
	    stmt = Allocate::make(collected_name, buf.type(), extents, const_true(), op->body);
            stmt = op->body;

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
