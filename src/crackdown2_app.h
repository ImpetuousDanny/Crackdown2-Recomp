// crackdown2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <memory>

#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>

#include "graphics_menu.h"

void InstallQuietKernelStubs();
void LoadRequestedGpuBackend(rex::RuntimeConfig& config);

class Crackdown2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Crackdown2App>(new Crackdown2App(ctx, "crackdown2",
        PPCImageConfig));
  }

  // Swap the SDK's logging stubs for KeSave/RestoreFloatingPointState with
  // silent no-ops before the XEX import table is resolved (see hooks.cpp).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    InstallQuietKernelStubs();
    LoadRequestedGpuBackend(config);
  }

  // Build our custom overlays and bind a key to toggle them. This runs after the
  // ImGuiDrawer exists, alongside the SDK's own overlays (debug F3, settings F4).
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    graphics_menu_ = std::make_unique<GraphicsMenu>(drawer);
    rex::ui::RegisterBind("bind_graphics_menu", "F5", "Toggle the Graphics menu",
                          [this] { graphics_menu_->ToggleVisible(); });
  }

  void OnShutdown() override { rex::ui::UnregisterBind("bind_graphics_menu"); }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

 private:
  std::unique_ptr<GraphicsMenu> graphics_menu_;
};
