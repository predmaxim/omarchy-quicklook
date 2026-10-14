#include "Pools.h"

namespace ql::pools {

QThreadPool *probe()
{
    static QThreadPool pool;
    static const bool init = (pool.setMaxThreadCount(2), true);
    Q_UNUSED(init)
    return &pool;
}

QThreadPool *heavy()
{
    static QThreadPool pool;
    static const bool init = (pool.setMaxThreadCount(3), true);
    Q_UNUSED(init)
    return &pool;
}

std::atomic<quint64> &currentGeneration()
{
    static std::atomic<quint64> generation{0};
    return generation;
}

} // namespace ql::pools
