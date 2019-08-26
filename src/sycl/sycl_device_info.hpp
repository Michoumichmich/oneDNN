/*******************************************************************************
* Copyright 2019 Intel Corporation
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

#ifndef SYCL_DEVICE_INFO_HPP
#define SYCL_DEVICE_INFO_HPP

#include <vector>
#include <CL/sycl.hpp>

#include "compute/device_info.hpp"
#include "ocl/ocl_utils.hpp"

namespace dnnl {
namespace impl {
namespace sycl {

class sycl_device_info_t : public compute::device_info_t {
public:
    sycl_device_info_t(const cl::sycl::device &device)
        : device_(device)
        , ext_(0)
        , eu_count_(0)
        , hw_threads_(0)
        , runtime_version_ {0, 0, 0} {}

    virtual status_t init() override {
        // Extensions
        for (uint64_t i_ext = 1; i_ext < (uint64_t)compute::device_ext_t::last;
                i_ext <<= 1) {
            const char *s_ext = ext2cl_str((compute::device_ext_t)i_ext);
            if (s_ext != nullptr && device_.has_extension(s_ext)) {
                ext_ |= i_ext;
            }
        }

        // EU count
        eu_count_
                = device_.get_info<cl::sycl::info::device::max_compute_units>();

        // Gen9 value, for GPU, for now
        int threads_per_eu = (device_.is_gpu() ? 7 : 1);
        hw_threads_ = eu_count_ * threads_per_eu;

        // Runtime version
        auto driver_version
                = device_.get_info<cl::sycl::info::device::driver_version>();
        if (runtime_version_.set_from_string(driver_version.c_str())
                != status::success) {
            runtime_version_.major = 0;
            runtime_version_.minor = 0;
            runtime_version_.build = 0;
        }

        return status::success;
    }

    virtual bool has(compute::device_ext_t ext) const override {
        return ext_ & (uint64_t)ext;
    }

    virtual int eu_count() const override { return eu_count_; }
    virtual int hw_threads() const override { return hw_threads_; }

    virtual const compute::runtime_version_t &runtime_version() const override {
        return runtime_version_;
    }

private:
    cl::sycl::device device_;
    uint64_t ext_;
    int32_t eu_count_, hw_threads_;
    compute::runtime_version_t runtime_version_;
};

} // namespace sycl
} // namespace impl
} // namespace dnnl

#endif // SYCL_DEVICE_INFO_HPP
