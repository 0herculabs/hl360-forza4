// fm4 - Forza Motorsport 4 recompiled for PC.

#include "generated/default/fm4_init.h"

#include "fm4_app.h"

std::unique_ptr<rex::ui::WindowedApp> Fm4App::Create(rex::ui::WindowedAppContext& ctx) {
  return std::unique_ptr<Fm4App>(new Fm4App(ctx, "fm4", PPCImageConfig));
}

REX_DEFINE_APP(fm4, Fm4App::Create)
