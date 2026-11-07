// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
int runtime_log_init(void);
void runtime_log_write(const char *line);
void runtime_log_shutdown(void);
