// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file BsdfUtil.isph

#pragma once

// Controls whether a single lane or all lanes are printed when
// using the -print_bsdf moonray command line option.
#if 1
#define BSDF_UTIL_EXTRACT(val) extract(val, 0)
#else
#define BSDF_UTIL_EXTRACT(val) val
#endif




