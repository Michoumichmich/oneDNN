/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
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

#include <memory>
#include <mutex>

#include "dnnl_thread.hpp"
#include "engine.hpp"
#include "utils.hpp"

#include "cpu/cpu_engine.hpp"

#include "scratchpad.hpp"

namespace dnnl {
namespace impl {

/* Allocating memory buffers on a page boundary to reduce TLB/page misses */
const size_t page_size = 2097152;

engine_t *get_cpu_engine() {
    static std::unique_ptr<engine_t> cpu_engine;
    static std::once_flag initialized;
    std::call_once(initialized, [&]() {
        engine_t *cpu_engine_ptr;
        cpu::cpu_engine_factory_t f;
        auto status = f.engine_create(&cpu_engine_ptr, 0);
        assert(status == status::success);
        MAYBE_UNUSED(status);
        cpu_engine.reset(cpu_engine_ptr);
    });
    return cpu_engine.get();
}

memory_storage_t *create_scratchpad_memory_storage(
        engine_t *engine, size_t size, size_t alignment) {
    // XXX: if engine is a non-native CPU engine (read: SYCL) then create
    // scratchpad through other, native CPU engine.
    //
    // SYCL CPU engine has asynchronous execution, and the library has to
    // extend (if needed) primitive lifetime until a kernel is completed.
    // For that, the library implements a reference-counting mechanism for
    // primitives (including internal scratchpads). In some cases a
    // scratchpad has to be destroyed from inside a kernel. This doesn't
    // play well with SYCL runtime, so switching to native CPU engine for such
    // cases.
    engine_t *mem_engine
            = (engine->kind() == engine_kind::cpu
                      && engine->backend_kind() != backend_kind::native)
            ? get_cpu_engine()
            : engine;

    memory_storage_t *mem_storage_ptr;
    auto status = mem_engine->create_memory_storage(
            &mem_storage_ptr, size, page_size);
    assert(status == status::success);
    MAYBE_UNUSED(status);
    return mem_storage_ptr;
}

/*
  Implementation of the scratchpad_t interface that is compatible with
  a concurrent execution
*/
struct concurrent_scratchpad_t : public scratchpad_t {
    concurrent_scratchpad_t(engine_t *engine, size_t size) : size_(size) {
        auto *mem_storage_ptr
                = create_scratchpad_memory_storage(engine, size, page_size);
        mem_storage_.reset(mem_storage_ptr);
    }

    virtual const memory_storage_t *get_memory_storage() const override {
        return mem_storage_.get();
    }

    //virtual char *get() const { return scratchpad_; }

private:
    std::unique_ptr<memory_storage_t> mem_storage_;
    size_t size_;

    DNNL_DISALLOW_COPY_AND_ASSIGN(concurrent_scratchpad_t);
};

/*
  Implementation of the scratchpad_t interface that uses a global
  scratchpad
*/

struct global_scratchpad_t : public scratchpad_t {
    global_scratchpad_t(engine_t *engine, size_t size) {
        // TODO: check if engine is the same
        if (size > size_) {
            size_ = size;
            memory_storage_t *mem_storage_ptr;
            auto status = engine->create_memory_storage(
                    &mem_storage_ptr, size, page_size);
            assert(status == status::success);
            MAYBE_UNUSED(status);
            mem_storage_.reset(mem_storage_ptr);
        }
        reference_count_++;
    }

    ~global_scratchpad_t() {
        reference_count_--;
        if (reference_count_ == 0) {
            mem_storage_.reset();
            size_ = 0;
        }
    }

    virtual const memory_storage_t *get_memory_storage() const override {
        return mem_storage_.get();
    }

    //virtual char *get() const { return scratchpad_; }

private:
    thread_local static std::unique_ptr<memory_storage_t> mem_storage_;
    thread_local static size_t size_;
    thread_local static unsigned int reference_count_;
};

thread_local std::unique_ptr<memory_storage_t>
        global_scratchpad_t::mem_storage_;
thread_local size_t global_scratchpad_t::size_ = 0;
thread_local unsigned int global_scratchpad_t::reference_count_ = 0;

/*
   Scratchpad creation routine
*/
scratchpad_t *create_scratchpad(engine_t *engine, size_t size) {
#ifndef DNNL_ENABLE_CONCURRENT_EXEC
    return new global_scratchpad_t(engine, size);
#else
    return new concurrent_scratchpad_t(engine, size);
#endif
}
} // namespace impl
} // namespace dnnl
