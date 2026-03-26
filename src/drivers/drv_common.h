/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SMAP: tracking_common
 */

#ifndef __TRACKING_COMMON_H__
#define __TRACKING_COMMON_H__

typedef u16 actc_t;

#define ACTC_WHITE_LIST_BIT  ((actc_t)(1u << 15))
#define ACTC_FREQ_MASK       ((actc_t)(~ACTC_WHITE_LIST_BIT))

#endif /* __TRACKING_COMMON_H__ */