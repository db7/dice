# Copyright (C) 2025 Huawei Technologies Co., Ltd.
# SPDX-License-Identifier: 0BSD

configure_file(dice dice)

# for tests
set(DICE_SCRIPT
    ${PROJECT_SOURCE_DIR}/scripts/dice
    PARENT_SCOPE)
