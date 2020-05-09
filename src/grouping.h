#ifndef PPCG_GROUPING_H

#include <isl/schedule.h>

#include "ppcg_options.h"

__isl_give isl_schedule *ppcg_compute_grouping_schedule(
	__isl_take isl_schedule_constraints *sc,
	__isl_keep isl_schedule *schedule, struct ppcg_options *options);

#endif
