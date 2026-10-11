
#pragma once

#include "dantes_inferno_app.h"
#include "native_renderer/dante_graphics_system.h"

class DantesInfernoNativeApp : public DantesInfernoApp {
 public:
  using DantesInfernoApp::DantesInfernoApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DantesInfernoNativeApp>(
        new DantesInfernoNativeApp(ctx, "dantes_inferno", PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    DantesInfernoApp::OnPreSetup(config);
    dante::ApplyRenderScaleConfig();
    if (auto graphics = dante::CreateConfiguredGraphicsSystem()) {
      config.graphics = std::move(graphics);
    }
  }
};
