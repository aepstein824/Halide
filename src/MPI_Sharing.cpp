#include "MPI_Sharing.h"
#include "IRMutator.h"

#include <iostream>

namespace Halide {
namespace Internal {

    class MPI_Sharing : public IRMutator {
    public:
	using IRMutator::visit;
	void visit(const MPI_Share *op) {
	    std::cout << "Place holder, should replace with calls.\n";
	}
    };
	

    Stmt mpi_sharing(Stmt s) {
	// use blocks and evaluate/call to insert ext calls
	s = MPI_Sharing().mutate(s);
	return s;
    }
}
}
