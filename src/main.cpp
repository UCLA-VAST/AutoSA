#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <isl/ctx.h>
#include <isl/id.h>
#include <isl/val.h>
#include <isl/set.h>
#include <isl/union_set.h>
#include <isl/union_map.h>
#include <isl/aff.h>
#include <isl/flow.h>
#include <isl/options.h>
#include <isl/schedule.h>
#include <isl/ast.h>
#include <isl/id_to_ast_expr.h>
#include <isl/ast_build.h>
#include <isl/schedule.h>
#include <isl/arg.h>
#include <isl/options.h>
#include <pet.h>
#include "ppcg.h"
#include "ppcg_options.h"
//#include "cuda.h"
//#include "opencl.h"
//#include "cpu.h"

#include <iostream>

using namespace std;

int main(int argc, char **argv)
{
	int r;

	r = autosa_main_wrap(argc, argv);

	return r;
}
