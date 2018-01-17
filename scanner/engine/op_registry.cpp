
#include "scanner/engine/op_registry.h"

namespace scanner {
namespace internal {

Result OpRegistry::add_op(const std::string& name, OpInfo* info) {
  Result result;
  result.set_success(true);
  if (ops_.count(name) > 0) {
    RESULT_ERROR(&result, "Attempted to re-register op %s", name.c_str());
    return result;
  }
  if (info->input_columns().empty() && !info->variadic_inputs()) {
    RESULT_ERROR(&result,
                 "Attempted to register op %s with empty input columns",
                 name.c_str());
    return result;
  }
  if (info->output_columns().empty()) {
    RESULT_ERROR(&result,
                 "Attempted to register op %s with empty output columns",
                 name.c_str());
    return result;
  }
  ops_.insert({name, info});
  return result;
}

OpInfo* OpRegistry::get_op_info(const std::string& name) const {
  return ops_.at(name);
}

bool OpRegistry::has_op(const std::string& name) const {
  return ops_.count(name) > 0;
}

OpRegistry* get_op_registry() {
  static OpRegistry* registry = new OpRegistry;
  return registry;
}
}
}
