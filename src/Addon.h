/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <kodi/AddonBase.h>

// Add-on entry point: Kodi creates one PVR instance per configured EPGStation server.
class ATTR_DLL_LOCAL CEpgstationAddon : public kodi::addon::CAddonBase
{
public:
  CEpgstationAddon() = default;

  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override;
};
