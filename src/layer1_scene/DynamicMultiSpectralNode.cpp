#include "vsgraysim/layer1_scene/DynamicMultiSpectralNode.h"

namespace vsgraysim {

void DynamicMultiSpectralNode::enqueueUpdate(const PendingDynUpdate& u) {
    std::lock_guard<std::mutex> lk(mutex_);
    pending_.push_back(u);
}

std::vector<PendingDynUpdate> DynamicMultiSpectralNode::drainPending() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<PendingDynUpdate> out;
    out.swap(pending_);
    return out;
}

} // namespace vsgraysim
