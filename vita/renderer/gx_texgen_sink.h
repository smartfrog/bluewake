// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gx_texgen.h"
#include "gxruntime/gxcore/gxcore.hpp"

namespace gx_vita {

class TexGenSink final : public gxruntime::aurora_recomp::AuroraRenderSink {
public:
  TexGenSink() { core_.set_plan_observer(observe_plan, this); }

  void set_guest_resolver(const DolGuestAddressResolver* resolver) {
    core_.set_guest_resolver(resolver);
  }

  void set_plan_observer(gxruntime::gxcore::GxCoreSink::PlanObserver observer, void* user) {
    observer_ = observer;
    user_ = user;
  }

  void set_copy_observer(gxruntime::gxcore::GxCoreSink::CopyObserver observer, void* user) {
    core_.set_copy_observer(observer, user);
  }

  bool submit_packet(const gxruntime::aurora_recomp::RenderPacket& packet) override {
    const bool accepted = core_.submit_packet(packet);
    // A Draw packet flushes the previous plan before installing its own snapshot.
    if (packet.kind == gxruntime::aurora_recomp::RenderPacketKind::Draw)
      pending_reason_ = texgen::post_support_reason(packet.draw.post_tex_mask,
                                                   packet.draw.post_tex_normalize);
    return accepted;
  }

  void flush_frame() { core_.flush_frame(); }

  const gxruntime::gxcore::GapCounters& counters() const { return core_.counters(); }

private:
  static void observe_plan(const gxruntime::gxcore::DrawPlan& plan, void* user) {
    auto& self = *static_cast<TexGenSink*>(user);
    if (self.observer_ == nullptr)
      return;
    if (plan.ok && self.pending_reason_ != nullptr) {
      ++self.core_.counters().unsupported_texgen;
      gxruntime::gxcore::DrawPlan refused;
      refused.skip_reason = self.pending_reason_;
      self.observer_(refused, self.user_);
      return;
    }
    self.observer_(plan, self.user_);
  }

  gxruntime::gxcore::GxCoreSink core_;
  gxruntime::gxcore::GxCoreSink::PlanObserver observer_ = nullptr;
  void* user_ = nullptr;
  const char* pending_reason_ = nullptr;
};

} // namespace gx_vita
