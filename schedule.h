#ifndef _SCHEDULE_H
#define _SCHEDULE_H

#include <isl/id.h>
#include <isl/space.h>

__isl_give isl_set *parametrization(__isl_take isl_space *space,
	int len, int first, __isl_keep isl_id_list *names);

#endif
