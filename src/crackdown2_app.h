// crackdown2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <memory>

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>

#include "graphics_menu.h"

void InstallQuietKernelStubs();
void InstallSerializedXInput();
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
    InstallSerializedXInput();
    LoadRequestedGpuBackend(config);
  }

  // Build our custom overlays and bind a key to toggle them. This runs after the
  // ImGuiDrawer exists, alongside the SDK's own overlays (debug F3, settings F4).
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    graphics_menu_ = std::make_unique<GraphicsMenu>(drawer);
    rex::ui::RegisterBind("bind_graphics_menu", "F5", "Toggle the Graphics menu",
                          [this] { graphics_menu_->ToggleVisible(); });

    // The SDK's keyboard/mouse driver only captures the mouse for the right
    // stick while "mouse look" is active, and nothing in the runtime turns it
    // on. Start with it on whenever --mnk_mouse is set, and let F6 release the
    // cursor again for the ImGui overlays.
    if (rex::cvar::Query<bool>("mnk_mouse")) {
      rex::input::mnk::SetMouseLookActive(true);
    }
    rex::ui::RegisterBind("bind_mouse_look", "F6", "Toggle mouse look (right stick on the mouse)",
                          [] { rex::input::mnk::SetMouseLookActive(!rex::input::mnk::IsMouseLookActive()); });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_graphics_menu");
    rex::ui::UnregisterBind("bind_mouse_look");
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

 private:
  std::unique_ptr<GraphicsMenu> graphics_menu_;
};
