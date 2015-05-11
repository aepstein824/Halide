#include "MPI_Sharing.h"
#include "IRMutator.h"

#include <iostream>

namespace Halide {
namespace Internal {

    class MPI_Sharing : public IRMutator {
    public:
	using IRMutator::visit;
	void visit(const MPI_Share *op) {
	    std::cout << "THE GAME =]\n";
	}
    };
	

    Stmt mpi_sharing(Stmt s) {
	s = MPI_Sharing().mutate(s);
	return s;
    }
}
}
