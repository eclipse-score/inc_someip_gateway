/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
#include "car_window_types.h"
#include "score/mw/com/impl/rust/bridge_macros.h"

BEGIN_EXPORT_MW_COM_INTERFACE(my_WindowInfoInterface, ::car_window_types::WindowInfoProxy,
                              ::car_window_types::WindowInfoSkeleton)
EXPORT_MW_COM_EVENT(my_WindowInfoInterface, ::car_window_types::WindowInfo, window_info_)
END_EXPORT_MW_COM_INTERFACE()
EXPORT_MW_COM_TYPE(my_WindowInfo, ::car_window_types::WindowInfo)

BEGIN_EXPORT_MW_COM_INTERFACE(my_WindowControlInterface, ::car_window_types::WindowControlProxy,
                              ::car_window_types::WindowControlSkeleton)
EXPORT_MW_COM_EVENT(my_WindowControlInterface, ::car_window_types::WindowControl, window_control_)
END_EXPORT_MW_COM_INTERFACE()
EXPORT_MW_COM_TYPE(my_WindowControl, ::car_window_types::WindowControl)
