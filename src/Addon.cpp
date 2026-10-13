/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Addon.h"

#include "Client.h"

ADDON_STATUS CEpgstationAddon::CreateInstance(const kodi::addon::IInstanceInfo& instance,
                                              KODI_ADDON_INSTANCE_HDL& hdl)
{
  if (!instance.IsType(ADDON_INSTANCE_PVR))
    return ADDON_STATUS_UNKNOWN;
  hdl = new CEpgstationClient(instance);
  return ADDON_STATUS_OK;
}

ADDONCREATOR(CEpgstationAddon)
