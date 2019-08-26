/*******************************************************************************
* Copyright 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "primitive_exec_types.hpp"
#include "engine.hpp"
#include "memory.hpp"
#include "memory_storage.hpp"
#include "primitive.hpp"

namespace dnnl {
namespace impl {

status_t cvt_primtive_args(const primitive_desc_t *pd, int nargs,
        const dnnl_exec_arg_t *c_args, exec_args_t &args) {
    using namespace status;

    if (!IMPLICATION(nargs > 0, c_args != nullptr)) return invalid_arguments;

    int n_inputs = 0;
    int n_outputs = 0;

    for (int i = 0; i < nargs; ++i) {
        int arg = c_args[i].arg;
        auto *mem = c_args[i].memory;

        switch (pd->arg_usage(arg)) {
            case primitive_desc_t::arg_usage_t::input:
                if (args.count(arg) != 0) return invalid_arguments;
                args[arg] = {mem, true};
                n_inputs++;
                break;
            case primitive_desc_t::arg_usage_t::output:
                if (args.count(arg) != 0) return invalid_arguments;
                args[arg] = {mem, false};
                n_outputs++;
                break;
            case primitive_desc_t::arg_usage_t::unused: break;
        }
    }

    bool scratchpad_required = !types::is_zero_md(pd->scratchpad_md());

    if (n_inputs != pd->n_inputs()) return invalid_arguments;
    if (n_outputs != pd->n_outputs() + (scratchpad_required ? 1 : 0))
        return invalid_arguments;

    return success;
}

memory_t *exec_ctx_t::input(int arg) const {
    if (args_.count(arg) != 1) return nullptr;
    const auto ma = args_.at(arg);
    assert(ma.is_const);
    return ma.mem;
}

memory_t *exec_ctx_t::output(int arg) const {
    if (args_.count(arg) != 1) return nullptr;
    const auto ma = args_.at(arg);
    assert(!ma.is_const);
    return ma.mem;
}

memory_t *exec_ctx_t::memory(int arg) const {
    assert(args_.count(arg) == 1);
    const auto ma = args_.at(arg);
    assert(!ma.is_const);
    return ma.mem;
}

void exec_ctx_t::register_memory_storage_mapping(
        const memory_storage_t *mem_storage, void *data) {
    assert(memory_storage_mapping_.count(mem_storage->impl()) == 0);
    memory_storage_mapping_.insert({mem_storage->impl(), data});
}

void *exec_ctx_t::host_ptr(int arg) const {
    if (args_.count(arg) != 1) return nullptr;

    auto *mem = args_.at(arg).mem;
    auto *mem_storage = mem->memory_storage();
    return host_ptr(mem_storage);
}

void *exec_ctx_t::host_ptr(const memory_storage_t *mem_storage) const {
    if (!*mem_storage) return nullptr;

    uint8_t *base_ptr = nullptr;
    if (memory_storage_mapping_.count(mem_storage->impl()) > 0) {
        base_ptr = static_cast<uint8_t *>(
                memory_storage_mapping_.at(mem_storage->impl()));
    } else {
        assert(mem_storage->engine()->backend_kind() == backend_kind::native);
        base_ptr = static_cast<uint8_t *>(mem_storage->data_handle());
    }
    return base_ptr + mem_storage->offset();
}

void *exec_ctx_t::map_memory_storage(const memory_storage_t *storage) const {
    if (!*storage) return nullptr;

    if (memory_storage_mapping_.count(storage->impl()) > 0) {
        return host_ptr(storage);
    }

    void *mapped_ptr;
    status_t status = storage->map_data(&mapped_ptr);
    assert(status == status::success);
    MAYBE_UNUSED(status);
    return mapped_ptr;
}

void exec_ctx_t::unmap_memory_storage(
        const memory_storage_t *storage, void *mapped_ptr) const {
    if (!*storage || memory_storage_mapping_.count(storage->impl()) > 0) return;

    status_t status = storage->unmap_data(mapped_ptr);
    assert(status == status::success);
    MAYBE_UNUSED(status);
}

void exec_ctx_t::set_scratchpad_grantor(
        const memory_tracking::grantor_t &scratchpad_grantor) {
    scratchpad_grantor_ = utils::make_unique<memory_tracking::grantor_t>(
            scratchpad_grantor);
}

const memory_tracking::grantor_t &exec_ctx_t::get_scratchpad_grantor() const {
    assert(scratchpad_grantor_.get());
    return *(scratchpad_grantor_.get());
}
} // namespace impl
} // namespace dnnl
