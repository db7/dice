# Copyright (C) 2025 Huawei Technologies Co., Ltd.                             #
# SPDX-License-Identifier: 0BSD                                                #

add_subdirectory(cli)
if(DICE_BUILD_LEGACY OR DICE_TESTS OR DICE_BENCHMARKS)
  add_subdirectory(dice)
  add_subdirectory(mod)
else()
  add_subdirectory(dice EXCLUDE_FROM_ALL)
  add_subdirectory(mod EXCLUDE_FROM_ALL)
endif()
