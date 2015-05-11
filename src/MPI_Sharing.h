#ifndef HALIDE_MPI_SHARING_H
#define HALIDE_MPI_SHARING_H

/** \file
 * Defines the bounds_inference lowering pass.
 */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a partially lowered and turn the MPI_Share into a call.
 */
Stmt mpi_sharing(Stmt);

}
}

#endif
