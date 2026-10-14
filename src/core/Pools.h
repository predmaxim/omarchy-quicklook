#pragma once
// Two thread pools + the current generation (spec: "Процесс").
#include <QThreadPool>
#include <atomic>

namespace ql::pools {

QThreadPool *probe();                        // 2 threads: resolve, head, type, stat, thumbnails, headers, page-0, office XML, TagLib, text
QThreadPool *heavy();                        // 3 threads: image/frame decode, PDF page render
std::atomic<quint64> &currentGeneration();   // written only by the GUI thread (Controller)
inline bool isCurrent(quint64 generation) { return currentGeneration().load() == generation; }

} // namespace ql::pools
