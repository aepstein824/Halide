#include <iostream> //AE: testing
#include <sstream>

#include "StorageFlattening.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Bounds.h"
#include "Simplify.h"


namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::map;
    using std::cout; //AE
namespace {
// Visitor and helper function to test if a piece of IR uses an extern image.
class UsesExternImage : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *c) {
        if (c->call_type == Call::Image) {
            result = true;
        } else {
            IRVisitor::visit(c);
        }
    }
public:
    UsesExternImage() : result(false) {}
    bool result;
};

inline bool uses_extern_image(Stmt s) {
    UsesExternImage uses;
    s.accept(&uses);
    return uses.result;
}
}

    std::vector<StorageSplit> rebalanceTree (std::vector<StorageSplit> splits) {
        // very similar to code in Lower.cpp in build_provide_loop_nest
        for (size_t i = 0; i < splits.size(); i++) {
            for (size_t j = i + 1; j < splits.size(); j++) {
                StorageSplit &first = splits[i];
                StorageSplit &second = splits[j];
                if (first.outer == second.old_var) {
                    // Given two splits:
                    // X  ->  a * Xo  + Xi
                    // (splits stuff other than Xo, including Xi)
                    // Xo ->  b * Xoo + Xoi
                    // Re-write to:
                    // X  -> ab * Xoo + s0
                    // s0 ->  a * Xoi + Xi
                    // (splits on stuff other than Xo, including Xi)
                    // The name Xo went away, because it was legal for it to
                    // be X before, but not after.
                    second.old_var = unique_name('s');
                    first.outer   = second.outer;
                    second.outer  = second.inner;
                    second.inner  = first.inner;
                    first.inner   = second.old_var;
                    Expr f = simplify(first.factor * second.factor);
                    second.factor = first.factor;
                    first.factor  = f;
                    // Push the second split back to just after the first
                    for (size_t k = j; k > i+1; k--) {
                        std::swap(splits[k], splits[k-1]);
                    }
                }
            }
        }
        return splits;
    }

class FlattenDimensions : public IRMutator {
public:
    FlattenDimensions(const string &output, const map<string, Function> &e)
        : output(output), env(e) {}
    Scope<int> scope;
private:
    const string &output;
    const map<string, Function> &env;
    Scope<int> realizations;

    Expr flatten_args(const string &name, const vector<Expr> &callArgs,
                      bool internal) {
        Expr idx = 0;
        vector<Expr> mins, strides;

        //AE: experimental
        map<string, Function>::const_iterator iter = env.find(name);
        // if it's internal, then it doesn't have the splitting through realize
        if(internal && iter != env.end()){
            vector<StorageSplit> splits = iter->second.schedule().storage_splits();
            splits = rebalanceTree(splits);
            
            const vector<string> &storage_dims = iter->second.schedule().storage_dims();
            
            for (size_t i = 0; i < storage_dims.size(); i++) {
                string dim = int_to_string(i);
                string stride_name = name + ".stride." + dim;
                string min_name = name + ".min." + dim;
                string stride_name_constrained = stride_name + ".constrained";
                string min_name_constrained = min_name + ".constrained";
                if (scope.contains(stride_name_constrained)) {
                    stride_name = stride_name_constrained;
                }
                if (scope.contains(min_name_constrained) && i < callArgs.size()) {
                    min_name = min_name_constrained;
                }
                strides.push_back(Variable::make(Int(32), stride_name));
                if(i < callArgs.size()) mins.push_back(Variable::make(Int(32), min_name));
            }
            
            const vector<string> &funcArgs = iter->second.args();
            for (size_t i = 0; i < funcArgs.size(); i++) {
                string search = funcArgs[i];
                bool insideSplit = false;
                Expr lastSplit;
                bool shouldSearchSplits = true;
                while (shouldSearchSplits) {
                    shouldSearchSplits = false;
                    for (size_t j = 0; j < splits.size(); j++) {
                        if (splits[j].old_var == search) {
                            Expr splitVal = callArgs[i] - mins[i];
                            if (insideSplit) {
                                splitVal = splitVal % lastSplit;
                            }
                            splitVal /= splits[j].factor;
                            for (size_t k = 0; k < storage_dims.size(); k++) {
                                //AE:
                                if (splits[j].outer == storage_dims[k]) {
                                    idx += (splitVal * strides[k]);
                                }
                            }
                            search = splits[j].inner;
                            shouldSearchSplits = true;
                            lastSplit = splits[j].factor;
                            insideSplit = true;
                        }
                    }
                }
                for (size_t k = 0; k < storage_dims.size(); k++) {
                    if (search == storage_dims[k]) {
                        //AE
                        Expr splitVal = callArgs[i] - mins[i];
                        if (insideSplit) {
                            splitVal = splitVal % lastSplit;
                        }
                        idx += splitVal * strides[k];
                    }
                }

            }
        } else {
            for (size_t i = 0; i < callArgs.size(); i++) {
                string dim = int_to_string(i);
                string stride_name = name + ".stride." + dim;
                string min_name = name + ".min." + dim;
                string stride_name_constrained = stride_name + ".constrained";
                string min_name_constrained = min_name + ".constrained";
                if (scope.contains(stride_name_constrained)) {
                    stride_name = stride_name_constrained;
                }
                if (scope.contains(min_name_constrained)) {
                    min_name = min_name_constrained;
                }
                strides.push_back(Variable::make(Int(32), stride_name));
                mins.push_back(Variable::make(Int(32), min_name));
            }

            if (internal) {
                // f(x, y) -> f[(x-xmin)*xstride + (y-ymin)*ystride] This
                // strategy makes sense when we expect x to cancel with
                // something in xmin.  We use this for internal allocations
                for (size_t i = 0; i < callArgs.size(); i++) {
                    idx += (callArgs[i] - mins[i]) * strides[i];
                }
            } else {
                // f(x, y) -> f[x*stride + y*ystride - (xstride*xmin +
                // ystride*ymin)]. The idea here is that the last term
                // will be pulled outside the inner loop. We use this for
                // external buffers, where the mins and strides are likely
                // to be symbolic
                Expr base = 0;
                for (size_t i = 0; i < callArgs.size(); i++) {
                    idx += callArgs[i] * strides[i];
                    base += mins[i] * strides[i];
                }
                idx -= base;
            }
        }

        return idx;
    }

    using IRMutator::visit;

    void visit(const Realize *realize) {
        realizations.push(realize->name, 1);

        Stmt body = mutate(realize->body);

        // this is going to hold the new bounds determined by splits
        Region splitBounds;

        map<string, Function>::const_iterator iter = env.find(realize->name);
        internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
        vector<StorageSplit> splits = iter->second.schedule().storage_splits();
        splits = rebalanceTree(splits);

        const vector<string> &storage_dims = iter->second.schedule().storage_dims();
        const vector<string> &args = iter->second.args();
                
        for (size_t i = 0; i < storage_dims.size(); i++) {
            /*
             * Three cases for a variable in the storage_dims.
             * 1) It is unsplit. It will appear in the args, and its bound comes
             *  from the old bounds extent.
             * 2) Outer. Then it is bound(parent) / factor. Parent can be arg or
             *  a split. Due to rebalance, in a tree, all but 1 will be this
             * 3) Inner. Extent is just factor.
             */
            int varCase = -1;
            string search;
            Expr const *num = 0;
            Expr const *den = 0;
            
            //first figure out if it's a case 2, because 2 needs info from inners
            // and possibly the args array
            for (size_t j = 0; j < splits.size(); j++) {
                if (storage_dims[i] == splits[j].outer) {
                    varCase = 2;
                    search = splits[j].old_var;
                    den = &splits[j].factor;
                    break;
                }
            }
            
            // at this point it must be either 1 or 3, so we are still searching
            //  for the variable name itself
            if (search.empty()) {
                search = storage_dims[i];
            }
            //if it was 2, look for its old val in the inners
            //otherwise, look for it as a case 3
            for (size_t j = 0; j < splits.size(); j++) {
                if (search == splits[j].inner) {
                    num = &splits[j].factor;
                    if (varCase < 0) {
                        // an inner that isn't also an outer is a case 3
                        varCase = 3;
                    }
                    break;
                }
            }
            
            //if it was 2 and we haven't found the inner, or we just haven't found it
            bool shouldSearchArgs = (varCase == 2 && num == 0) || (varCase < 0);
            for (size_t j = 0; shouldSearchArgs && j < args.size(); j++) {
                if (search == args[j]) {
                    num = &realize->bounds[j].extent;
                    if (varCase < 0) {
                        //if the first mention of the variable is in the
                        // args list, then it was never split
                        //currently unused, but good to know
                        varCase = 1;
                    }
                    break;
                }
            }
            internal_assert(num != 0);
            Expr splitMin(0);
            Expr splitExtent = *num;
            if (den != 0) {
                // integer division that rounds up
                splitExtent = simplify((((*num) - 1) / (*den)) + 1);
            }

            splitBounds.push_back(Range(splitMin, splitExtent));
        }
        // Compute the size
        std::vector<Expr> extents;
        for (size_t i = 0; i < splitBounds.size(); i++) {
          extents.push_back(splitBounds[i].extent);
          extents[i] = mutate(extents[i]);
        }
        Expr condition = mutate(realize->condition);

        realizations.pop(realize->name);

        internal_assert(storage_dims.size() == splitBounds.size());

        stmt = body;
        
        //AE: a separate buffer for each type?
        for (size_t idx = 0; idx < realize->types.size(); idx++) {
            string buffer_name = realize->name;
            if (realize->types.size() > 1) {
                buffer_name = buffer_name + '.' + int_to_string(idx);
            }

            // Make the names for the mins, extents, and strides
            int dims = splitBounds.size();
            int odims = realize->bounds.size(); //pre split dims
            vector<string> min_name(dims), extent_name(dims), stride_name(dims);
            //only need odims mins, because those are not affected by split
            for (int i = 0; i < dims; i++) {
                string d = int_to_string(i);
                if(i<odims) min_name[i] = buffer_name + ".min." + d;
                stride_name[i] = buffer_name + ".stride." + d;
                extent_name[i] = buffer_name + ".extent." + d;
            }
            vector<Expr> min_var(dims), extent_var(dims), stride_var(dims);
            for (int i = 0; i < dims; i++) {
                if(i<odims) min_var[i] = Variable::make(Int(32), min_name[i]);
                extent_var[i] = Variable::make(Int(32), extent_name[i]);
                stride_var[i] = Variable::make(Int(32), stride_name[i]);
            }

            // Promote the type to be a multiple of 8 bits
            Type t = realize->types[idx];
            t.bits = t.bytes() * 8;

            // Create a buffer_t object for this allocation.
            vector<Expr> args(dims*3 + 2);
            //args[0] = Call::make(Handle(), Call::null_handle, vector<Expr>(), Call::Intrinsic);
            Expr first_elem = Load::make(t, buffer_name, 0, Buffer(), Parameter());
            args[0] = Call::make(Handle(), Call::address_of, vec(first_elem), Call::Intrinsic);
            args[1] = realize->types[idx].bytes();
            for (int i = 0; i < dims; i++) {
                if(i<odims) args[3*i+2] = min_var[i];
                else args[3*i+2] = Expr(0);
                args[3*i+3] = extent_var[i];
                args[3*i+4] = stride_var[i];
            }
            Expr buf = Call::make(Handle(), Call::create_buffer_t,
                                  args, Call::Intrinsic);
            stmt = LetStmt::make(buffer_name + ".buffer",
                                 buf,
                                 stmt);
            // Make the allocation node
            stmt = Allocate::make(buffer_name, t, extents, condition, stmt);
            // Compute the strides
            for (int i = (int)splitBounds.size()-1; i > 0; i--) {
                Expr stride = stride_var[i-1] * extent_var[i-1];
                stmt = LetStmt::make(stride_name[i], stride, stmt);
            }

            // Innermost stride is one
            if (dims > 0) {
                //int innermost = storage_permutation.empty() ? 0 : storage_permutation[0];
                stmt = LetStmt::make(stride_name[0], 1, stmt);
            }
            

            // Assign the mins and extents stored
            for (size_t i = realize->bounds.size(); i > 0; i--) {
                stmt = LetStmt::make(min_name[i-1], realize->bounds[i-1].min, stmt);
            }
            
            for (size_t i = dims; i > 0; i--) {
                stmt = LetStmt::make(extent_name[i-1], splitBounds[i-1].extent, stmt);
            }
        }
    }

    struct ProvideValue {
        Expr value;
        string name;
    };

    void flatten_provide_values(vector<ProvideValue> &values, const Provide *provide) {
        values.resize(provide->values.size());

        for (size_t i = 0; i < values.size(); i++) {
            Expr value = mutate(provide->values[i]);

            // Promote the type to be a multiple of 8 bits
            Type t = value.type();
            t.bits = t.bytes() * 8;
            if (t.bits != value.type().bits) {
                value = Cast::make(t, value);
            }

            values[i].value = value;
            if (values.size() > 1) {
                values[i].name = provide->name + "." + int_to_string(i);
            } else {
                values[i].name = provide->name;
            }
        }
    }

    // Lower a set of provides
    Stmt flatten_provide_atomic(const Provide *provide) {
        vector<ProvideValue> values;
        flatten_provide_values(values, provide);

        Stmt result;
        for (size_t i = 0; i < values.size(); i++) {
            const ProvideValue &cv = values[i];

            Expr idx = mutate(flatten_args(cv.name, provide->args,
                                           provide->name != output));
            Expr var = Variable::make(cv.value.type(), cv.name + ".value");
            Stmt store = Store::make(cv.name, var, idx);

            if (result.defined()) {
                result = Block::make(result, store);
            } else {
                result = store;
            }
        }

        for (size_t i = values.size(); i > 0; i--) {
            const ProvideValue &cv = values[i-1];

            result = LetStmt::make(cv.name + ".value", cv.value, result);
        }
        return result;
    }

    Stmt flatten_provide(const Provide *provide) {
        vector<ProvideValue> values;
        flatten_provide_values(values, provide);

        Stmt result;
        for (size_t i = 0; i < values.size(); i++) {
            const ProvideValue &cv = values[i];

            Expr idx = mutate(flatten_args(cv.name, provide->args,
                                           provide->name != output));
            Stmt store = Store::make(cv.name, cv.value, idx);

            if (result.defined()) {
                result = Block::make(result, store);
            } else {
                result = store;
            }
        }
        return result;
    }

    void visit(const Provide *provide) {
        Stmt result;

        // Handle the provide atomically if necessary. This logic is
        // currently very conservative, it will lower many provides
        // atomically that do not require it.
        if (provide->values.size() == 1) {
            // If there is only one value, we don't need to worry
            // about atomicity.
            result = flatten_provide(provide);
        } else if (!realizations.contains(provide->name) &&
                   uses_extern_image(provide)) {
            // If the provide is not a realization and it uses an
            // input image, it might be aliased. Flatten it atomically
            // because we can't prove the boxes don't overlap.
            result = flatten_provide_atomic(provide);
        } else {
            Box provided = box_provided(Stmt(provide), provide->name);
            Box required = box_required(Stmt(provide), provide->name);

            if (boxes_overlap(provided, required)) {
                // The boxes provided and required might overlap, so
                // the provide must be done atomically.
                result = flatten_provide_atomic(provide);
            } else {
                // The boxes don't overlap.
                result = flatten_provide(provide);
            }
        }
        stmt = result;
    }

    void visit(const Call *call) {

        if (call->call_type == Call::Extern || call->call_type == Call::Intrinsic) {
            vector<Expr> args(call->args.size());
            bool changed = false;
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = mutate(call->args[i]);
                if (!args[i].same_as(call->args[i])) changed = true;
            }
            if (!changed) {
                expr = call;
            } else {
                expr = Call::make(call->type, call->name, args, call->call_type);
            }
        } else {
            string name = call->name;
            if (call->call_type == Call::Halide &&
                call->func.outputs() > 1) {
                name = name + '.' + int_to_string(call->value_index);
            }

            // Promote the type to be a multiple of 8 bits
            Type t = call->type;
            t.bits = t.bytes() * 8;

            Expr idx = mutate(flatten_args(name, call->args,
                                           env.find(call->name) != env.end()));
            expr = Load::make(t, name, idx, call->image, call->param);

            if (call->type.bits != t.bits) {
                expr = Cast::make(call->type, expr);
            }
        }
    }

    void visit(const LetStmt *let) {
        // Discover constrained versions of things.
        bool constrained_version_exists = ends_with(let->name, ".constrained");
        if (constrained_version_exists) {
            scope.push(let->name, 0);
        }

        IRMutator::visit(let);

        if (constrained_version_exists) {
            scope.pop(let->name);
        }
    }
};


Stmt storage_flattening(Stmt s, const string &output, const map<string, Function> &env) {
    return FlattenDimensions(output, env).mutate(s);
}

}
}
