/*
 * <one line to give the library's name and an idea of what it does.>
 * Copyright (C) 2017  Aetf <aetf@unlimitedcodeworks.xyz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef TFALLOCATOR_H
#define TFALLOCATOR_H

/*
 * Make sure tensorflow_headers is included first before
 * any other headers, so we can correctly override TF logging
 * with ours.
 */
#include "oplibraries/tensorflow/tensorflow_headers.h"

#include "resources/resources.h"
#include "utils/threadutils.h"

#include <memory>

namespace salus {
class ResourceContext;

namespace oplib::tensorflow {
class PerOpAllocator : public tf::Allocator, public tf::core::RefCounted
{
public:
    SALUS_DISALLOW_COPY_AND_ASSIGN(PerOpAllocator);

    static const std::string NamePrefix;

    static PerOpAllocator *downcast(tf::Allocator *);

    explicit PerOpAllocator(const std::shared_ptr<const ResourceContext> &rctx, tf::Allocator *other);
    explicit PerOpAllocator(std::shared_ptr<const ResourceContext> &&rctx, tf::Allocator *other);

    ~PerOpAllocator() override;

    std::string Name() override;
    void *AllocateRaw(size_t alignment, size_t num_bytes) override;
    void *AllocateRaw(size_t alignment, size_t num_bytes,
                      const tf::AllocationAttributes &allocation_attr) override;

    void DeallocateRaw(void *ptr) override;
    bool ShouldAllocateEmptyTensors() override;

    bool TracksAllocationSizes() override
    {
        return true;
    }

    size_t RequestedSize(void *ptr) override;

    tf::int64 AllocationId(void *ptr) override;

    const ResourceContext &resourceContext() const
    {
        return *m_rctx;
    }

    size_t lastFailedAllocSize() const
    {
        auto g = sstl::with_guard(m_mu);
        return m_lastFailedAllocSize;
    }

    size_t peakAllocSize() const
    {
        auto g = sstl::with_guard(m_mu);
        return m_peakAllocSize;
    }

private:
    void recordSize(void *ptr, size_t size, bool didRollback);
    size_t findSize(void *ptr);

    std::shared_ptr<const ResourceContext> m_rctx;

    tf::Allocator *m_actualAlloc;

    mutable std::mutex m_mu;
    std::unordered_map<void *, size_t> m_allocated GUARDED_BY(m_mu);

    size_t m_lastFailedAllocSize = 0 GUARDED_BY(m_mu);
    size_t m_peakAllocSize = 0 GUARDED_BY(m_mu);
    size_t m_currentAlloc = 0 GUARDED_BY(m_mu);
    size_t m_mismatchedResRequest = 0 GUARDED_BY(m_mu);
};

} // namespace oplib::tensorflow
} // namespace salus

#endif // TFALLOCATOR_H
