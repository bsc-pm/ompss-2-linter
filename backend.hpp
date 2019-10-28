#include <libgen.h>
#include <unistd.h>
#include <sys/types.h>


// AGGREGATION LEVEL == 3
// --------------------------------------------------------------

bool mtc_already_exist(
	const mtc_entry_t<acc_entry_t, dep_t *> *val,
	const mtc_entry_t<acc_entry_t, dep_t *> *other,
	uint64_t low, uint64_t high,
	const itvmap_t<mtc_entry_t<acc_entry_t, dep_t *>> *map
) {
	bool res = true
		// Same match type
		&& val->type == other->type
		// Access fragments having the same interval access mode
		&& val->entry_a.itv.mode == other->entry_a.itv.mode
		// Access fragments having the same details
		&& val->entry_a.pc     == other->entry_a.pc
		&& val->entry_a.imgid  == other->entry_a.imgid
		&& val->entry_a.opcode == other->entry_a.opcode
		&& (
			( // Deps exists
				val->entry_b && other->entry_b
				// Deps having the same name
				&& strcmp(val->entry_b->name, other->entry_b->name) == 0
				// Deps having the same extents
				&& val->entry_b->extent_low  == other->entry_b->extent_low
				&& val->entry_b->extent_high == other->entry_b->extent_high
			)
			||
			true
		)
	;
	// printf("Compare %s <%p + %lu> %p %u %lu %p VS %s <%p + %lu> %p %u %lu %p => "
	// 	"INTERSECT AT (%p + %lu) = %d\n",
	// 	itv_mode_str(val->entry_a.itv.mode), val->itv.lowptr, val->itv.high - val->itv.low,
	// 		(void *)val->entry_a.pc, val->entry_a.imgid, val->entry_a.opcode, val->entry_b,
	// 	itv_mode_str(other->entry_a.itv.mode), other->itv.lowptr, other->itv.high - other->itv.low,
	// 		(void *)other->entry_a.pc, other->entry_a.imgid, other->entry_a.opcode, other->entry_b,
	// 	(void *)low, high - low, res
	// );
	return res;
}


bool mtc_already_exist(
	const mtc_entry_t<dep_entry_t, dep_t *> *val,
	const mtc_entry_t<dep_entry_t, dep_t *> *other,
	uint64_t low, uint64_t high,
	const itvmap_t<mtc_entry_t<dep_entry_t, dep_t *>> *map
) {
	bool res = true
	// Same match type
	&& val->type == other->type
	// Dep fragments having the same interval access mode
	&& val->entry_a.itv.mode == other->entry_a.itv.mode
	// Deps having the same name
	&& strcmp(val->entry_a.dep->name, other->entry_a.dep->name) == 0
	// Deps having the same extents
	&& val->entry_a.dep->extent_low  == other->entry_a.dep->extent_low
	// NOTE: Commented: sometimes dependencies lack a precise size
	// information, so every new task will see an increasing value
	// for the 'extent_high' parameter because Nanos6 will only learn
	// it when increasing addresses are actually accessed.
	&& val->entry_a.dep->extent_high >= other->entry_a.dep->extent_high
	;
	// printf("%u %u Compare %s <%p + %lu> %s %p %p VS %s <%p + %lu> %s %p %p => "
	// 	"INTERSECT AT (%p + %lu) = %d\n",
	// 	val->type, other->type,
	// 	itv_mode_str(val->entry_a.itv.mode), val->itv.lowptr, val->itv.high - val->itv.low,
	// 		val->entry_a.dep->name, (void *)val->entry_a.dep->extent_low, (void *)val->entry_a.dep->extent_high,
	// 	itv_mode_str(other->entry_a.itv.mode), other->itv.lowptr, other->itv.high - other->itv.low,
	// 		other->entry_a.dep->name, (void *)other->entry_a.dep->extent_low, (void *)other->entry_a.dep->extent_high,
	// 	(void *)low, high - low, res
	// );
	return res;
}


bool mtc_acc_touch(
	const mtc_entry_t<acc_entry_t, dep_t *> *val,
	const mtc_entry_t<acc_entry_t, dep_t *> *other,
	uint64_t low, uint64_t high,
	const itvmap_t<mtc_entry_t<acc_entry_t, dep_t *>> *map
) {
	bool res = true
		&& val->type == other->type && low <= high
		// ALLOC/FREE must never merge
		&& val->entry_a.itv.mode > ITV_MODE_IGNORE
		&& val->entry_a.itv.mode == other->entry_a.itv.mode
		&& val->entry_a.pc == other->entry_a.pc
		&& val->entry_a.imgid == other->entry_a.imgid
		&& val->entry_a.opcode == other->entry_a.opcode
		&& (
			( // Deps exists
				val->entry_b && other->entry_b
				// Deps having the same name
				&& strcmp(val->entry_b->name, other->entry_b->name) == 0
				// Deps having the same extents
				&& val->entry_b->extent_low  == other->entry_b->extent_low
				&& val->entry_b->extent_high == other->entry_b->extent_high
			)
			||
			true
		)
	;
	// printf("%u %u Compare %s <%p + %lu> %s %p %p VS %s <%p + %lu> %s %p %p => "
	// 	"INTERSECT AT (%p + %lu) = %d\n",
	// 	val->type, other->type,
	// 	itv_mode_str(val->entry_a.itv.mode), val->itv.lowptr, val->itv.high - val->itv.low,
	// 		val->entry_b->name, (void *)val->entry_b->extent_low, (void *)val->entry_b->extent_high,
	// 	itv_mode_str(other->entry_a.itv.mode), other->itv.lowptr, other->itv.high - other->itv.low,
	// 		other->entry_b->name, (void *)other->entry_b->extent_low, (void *)other->entry_b->extent_high,
	// 	(void *)low, high - low, res
	// );

	return res;
}


// AGGREGATION LEVEL == 0+
// --------------------------------------------------------------


bool dep_match_temporal(
	const dep_entry_t *val_a, const dep_entry_t *val_b,
	uint64_t intersect_low, uint64_t intersect_high,
	uint64_t remainder_low, uint64_t remainder_high,
	const itvmap_t<mtc_dd_entry_t> *map_out
) {
	bool res = (true
		&& val_b
		&& intersect_low < intersect_high
		&& (
			// (val_b->itv.mode == ITV_MODE_IGNORE)
			// ||
			(val_b->itv.mode > ITV_MODE_IGNORE && val_a->itv.mode == val_b->itv.mode)
		)
		&& val_a->start_time < val_b->end_time
	);
	return res;
}


bool dep_match(
	const dep_entry_t *val_a, const dep_entry_t *val_b,
	uint64_t intersect_low, uint64_t intersect_high,
	uint64_t remainder_low, uint64_t remainder_high,
	const itvmap_t<mtc_dd_entry_t> *map_out
) {
	// printf("Compare %s <%p + %lu> VS %s <%p + %lu> => "
	// 	"INTERSECT AT (%p + %lu), REMAINDER AT (%p + %lu)\n",
	// 	itv_mode_str(val_a->itv.mode), val_a->itv.lowptr, val_a->itv.high - val_a->itv.low,
	// 	val_b ? itv_mode_str(val_b->itv.mode) : "-",
	// 	val_b ? val_b->itv.lowptr : 0x0,
	// 	val_b ? val_b->itv.high - val_b->itv.low : 0,
	// 	(void *)intersect_low, intersect_high - intersect_low,
	// 	(void *)remainder_low, intersect_low  - remainder_low
	// );
	bool res = (true
		&& val_b
		&& intersect_low < intersect_high
		&& val_b->itv.mode > ITV_MODE_IGNORE && val_a->itv.mode == val_b->itv.mode
	);
	return res;
}


bool dep_match_diff_mode(
	const dep_entry_t *val_a, const dep_entry_t *val_b,
	uint64_t intersect_low, uint64_t intersect_high,
	uint64_t remainder_low, uint64_t remainder_high,
	const itvmap_t<mtc_dd_entry_t> *map_out
) {
	bool res = (true
		&& val_b
		&& intersect_low < intersect_high
		&& val_a->itv.mode > ITV_MODE_IGNORE && val_b->itv.mode > ITV_MODE_IGNORE
		&& val_a->itv.mode != val_b->itv.mode
		&& val_a->end_time > val_b->start_time
	);
	// printf("Compare %p %s <%p + %lu> %s VS %s <%p + %lu> %s => "
	// 	"INTERSECT AT (%p + %lu), REMAINDER AT (%p + %lu) = %d\n",
	// 	val_a,
	// 	itv_mode_str(val_a->itv.mode), val_a->itv.lowptr, val_a->itv.high - val_a->itv.low, val_a->dep->name,
	// 	val_b ? itv_mode_str(val_b->itv.mode) : "-",
	// 	val_b ? val_b->itv.lowptr : 0x0,
	// 	val_b ? val_b->itv.high - val_b->itv.low : 0,
	// 	val_b ? val_b->dep->name : "-",
	// 	(void *)intersect_low, intersect_high - intersect_low,
	// 	(void *)remainder_low, intersect_low  - remainder_low,
	// 	res
	// );
	return res;
}


bool acc_match_dep(
	const acc_entry_t *val_a, const dep_entry_t *val_b,
	uint64_t intersect_low, uint64_t intersect_high,
	uint64_t remainder_low, uint64_t remainder_high,
	const itvmap_t<mtc_ad_entry_t> *map_out
) {
	bool res = (true
		&& val_b
		&& (
			// This is irrelevant if it is a release dep
			val_b->itv.mode == ITV_MODE_IGNORE
			||
			val_a->itv.mode == ITV_MODE_ALLOC || val_a->itv.mode == ITV_MODE_FREE
			||
			val_a->itv.mode == val_b->itv.mode
		)
		&& val_a->time > val_b->start_time
		// This is irrelevant if it is a weak or a release dep
		&& val_a->time < val_b->end_time
	);
	// printf("Compare %s <%p + %lu> (%lu, %lu) VS %s <%p + %lu> S(%lu, %lu) E(%lu, %lu) => "
	// 	"INTERSECT AT (%p + %lu), REMAINDER AT (%p + %lu) = %u\n",
	// 	itv_mode_str(val_a->itv.mode), val_a->itv.lowptr, val_a->itv.high - val_a->itv.low,
	// 	val_a->time.epoch, val_a->time.step,
	// 	val_b ? itv_mode_str(val_b->itv.mode) : "-",
	// 	val_b ? val_b->itv.lowptr : 0x0,
	// 	val_b ? val_b->itv.high - val_b->itv.low : 0,
	// 	val_b ? val_b->start_time.epoch: 0,
	// 	val_b ? val_b->start_time.step: 0,
	// 	val_b ? val_b->end_time.epoch: 0,
	// 	val_b ? val_b->end_time.step: 0,
	// 	(void *)intersect_low, intersect_high - intersect_low,
	// 	(void *)remainder_low, intersect_low  - remainder_low,
	// 	res
	// );
	return res;
}


bool acc_match_child_dep(
	const acc_entry_t *val_a, const dep_entry_t *val_b,
	uint64_t intersect_low, uint64_t intersect_high,
	uint64_t remainder_low, uint64_t remainder_high,
	const itvmap_t<mtc_ad_entry_t> *map_out
) {
	bool res = (true
		&& val_b
		// && (
		// 	// It is an ALLOC/FREE operation
		// 	val_a->itv.mode == ITV_MODE_ALLOC || val_a->itv.mode == ITV_MODE_FREE
		// 	||
		// 	// It is a REAL/WRITE access which coincide with the dependency
		// 	val_a->itv.mode == val_b->itv.mode
		// )
		&& val_a->time > val_b->start_time
		&& val_a->time < val_b->end_time
	);
	// printf("Compare %s <%p + %lu> VS %s <%p + %lu> from task id %u=> "
	// 	"INTERSECT AT (%p + %lu), REMAINDER AT (%p + %lu) [%u]\n",
	// 	itv_mode_str(val_a->itv.mode), val_a->itv.lowptr, val_a->itv.high - val_a->itv.low,
	// 	val_b ? itv_mode_str(val_b->itv.mode) : "-",
	// 	val_b ? val_b->itv.lowptr : 0x0,
	// 	val_b ? val_b->itv.high - val_b->itv.low : 0,
	// 	val_b ? val_b->dep->task->id : 0,
	// 	(void *)intersect_low, intersect_high - intersect_low,
	// 	(void *)remainder_low, intersect_low  - remainder_low,
	// 	res
	// );
	return res;
}


bool acc_match_RaW(
	const acc_entry_t *val_a, const mtc_ad_entry_t *val_b,
	uint64_t intersect_low, uint64_t intersect_high,
	uint64_t remainder_low, uint64_t remainder_high,
	const itvmap_t<mtc_entry_t<acc_entry_t, mtc_ad_entry_t>> *map_out
) {
	bool res = ( true
		&& val_b
		&& val_a->itv.mode == ITV_MODE_READ
		&& val_a->time > val_b->entry_a.time
	);
	return res;
}


bool acc_touch_and_are_equal(
	const acc_entry_t *val, const acc_entry_t *other,
	uint64_t low, uint64_t high,
	const itvmap_t<acc_entry_t> *map
) {
	bool res = (
		low <= high
		// ALLOC/FREE must never merge
		// && val->itv.mode > ITV_MODE_IGNORE
		&& val->itv.mode == other->itv.mode
		&& val->pc == other->pc
		&& val->imgid == other->imgid
		&& val->opcode == other->opcode
	);
	// printf("Compare %s <%p + %lu> VS %s <%p + %lu> => "
	// 	"INTERSECT AT (%p + %lu) = %d\n",
	// 	itv_mode_str(val->itv.mode), val->itv.lowptr, val->itv.high - val->itv.low,
	// 	itv_mode_str(other->itv.mode), other->itv.lowptr, other->itv.high - other->itv.low ,
	// 	(void *)low, high - low, res
	// );
	return res;
}


bool dep_intersect_and_same_mode(
	const dep_entry_t *val, const dep_entry_t *other,
	uint64_t low, uint64_t high,
	const itvmap_t<dep_entry_t> *map
) {
	bool res = (true
		&& low < high
		&& val->itv.mode > ITV_MODE_IGNORE
		&& val->itv.mode == other->itv.mode
	);
	return res;
}


bool dep_equal(dep_t * dep_a, dep_t * dep_b) {
return (
			( // Deps exists
				dep_a && dep_b
				// Deps having the same name
				&& strcmp(dep_a->name, dep_b->name) == 0
				// Deps having the same extents
				&& dep_a->extent_low  == dep_b->extent_low
				&& dep_a->extent_high == dep_b->extent_high
			)
			||
			true
		);
}


bool mtc_aggregate_same_addr_and_pc_lvl2(
	const mtc_entry_t<acc_entry_t, dep_t *> *val,
	const mtc_entry_t<acc_entry_t, dep_t *> *other,
	uint64_t low, uint64_t high,
	const itvmap_t<mtc_entry_t<acc_entry_t, dep_t *>> *map
) {
	return (true
		&& val->type == other->type
		&& val->entry_a.itv.mode == other->entry_a.itv.mode
		&& dep_equal(val->entry_b, other->entry_b)
		// Merge accesses coming from the same PC if the addresses
		// are contiguous
		&& (
			(true
				&& low <= high
				// ALLOC/FREE must never merge
				// && val->itv.mode > ITV_MODE_IGNORE
				&& val->entry_a.pc == other->entry_a.pc
			)
			||
			// Merge accesses coming from different PC if the range
			// is completely contained in the existing one
			(true
				&& itv_contains_itv(&other->itv, &val->itv)
			)
		)
	);
}


bool mtc_aggregate_same_addr_and_pc_lvl2(
	const mtc_entry_t<dep_entry_t, dep_t *> *val,
	const mtc_entry_t<dep_entry_t, dep_t *> *other,
	uint64_t low, uint64_t high,
	const itvmap_t<mtc_entry_t<dep_entry_t, dep_t *>> *map
) {
	return (true
		&& low <= high
		&& val->type == other->type
		&& val->entry_a.itv.mode == other->entry_a.itv.mode
		&& dep_equal(val->entry_b, other->entry_b)
	);
}


// AGGREGATION LEVEL == 1
// --------------------------------------------------------------

bool mtc_aggregate_same_addr_and_pc_lvl1(
	const mtc_entry_t<acc_entry_t, dep_t *> *val,
	const mtc_entry_t<acc_entry_t, dep_t *> *other,
	uint64_t low, uint64_t high,
	const itvmap_t<mtc_entry_t<acc_entry_t, dep_t *>> *map
) {
	return (true
		&& low <= high
		&& val->type == other->type
		&& val->entry_a.itv.mode == other->entry_a.itv.mode
		&& dep_equal(val->entry_b, other->entry_b)
		&& val->entry_a.pc == other->entry_a.pc
	);
}


bool mtc_aggregate_same_addr_and_pc_lvl1(
	const mtc_entry_t<dep_entry_t, dep_t *> *val,
	const mtc_entry_t<dep_entry_t, dep_t *> *other,
	uint64_t low, uint64_t high,
	const itvmap_t<mtc_entry_t<dep_entry_t, dep_t *>> *map
) {
	return (true
		&& low <= high
		&& val->type == other->type
		&& val->entry_a.itv.mode == other->entry_a.itv.mode
		&& dep_equal(val->entry_b, other->entry_b)
	);
}


template <typename entry_a_t, typename entry_b_t>
void insert_match(
	mtc_entry_t<entry_a_t, entry_b_t> *mtc_entry,
	itvmap_t<mtc_entry_t<entry_a_t, entry_b_t>> *map
) {
	/* if (config.aggregation_level == config.AGGREGATE_ADDR) {
		itvmap_insert_aggregated(
			*mtc_entry, map,
			mtc_aggregate_same_addr_and_pc_lvl2);
	}
	else if (config.aggregation_level == config.AGGREGATE_PC) {
		itvmap_insert_aggregated(
			*mtc_entry, map,
			mtc_aggregate_same_addr_and_pc_lvl1);
	}
	else */ {
		itvmap_insert(
			*mtc_entry, map
		);
	}
}


INLINE
bool opaque_obj_lookup(task_data_t *task, opaque_obj_t *obj, uint64_t low, uint64_t high) {
	for (auto &d : task->normal_deps) {
		dep_t *dep = d;

		if (low >= dep->extent_high || high <= dep->extent_low) {
			continue;
		}

		obj->extent_low = dep->extent_low;
		obj->extent_high = dep->extent_high;
		obj->name = dep->name;

		return true;
	}

	var_t *var = variable_find_by_itv(low, high);

	if (var == NULL || var->sec->img->invalid) {
		obj->extent_low = low;
		obj->extent_high = high;
		obj->name = "--";

		return false;
	}

	obj->extent_low = var->extent_low;
	obj->extent_high = var->extent_high;
	obj->name = var->name;

	return true;
}


const char *lookup_name_from_addr(task_data_t *task, uint64_t low, uint64_t high) {
	for (auto &d : task->normal_deps) {
		dep_t *dep = d;

		if (low >= dep->extent_high || high <= dep->extent_low) {
			continue;
		}

		return dep->name;
	}

	var_t *var = variable_find_by_itv(low, high);

	if (var == NULL) {
		return NULL;
	}

	if (var->sec->img->invalid) {
		return NULL;
	}

	return var->name;
}


/**
 * READ task->itvmap_unmatched
 * READ dep->itvmap
 * READ normal_strong_dep->itvmap_unmatched
 * REMOVE normal_strong_dep->itvmap_unmatched
 * INSERT task->itvmap_mtc_acc_dep
 * REMOVE task->itvmap_unmatched
 * CLEAR task->itvmap_unmatched
 * INSERT task->itvmap_unmatched
 */
void match_acc_with_deps(task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	itvmap_t<mtc_ad_entry_t> res;
	itvmap_t<mtc_entry_t<acc_entry_t, mtc_ad_entry_t>> res_raw;

	task->itvmap_common.insert(
		task->itvmap_sametask_only.begin(),
		task->itvmap_sametask_only.end()
	);

	task->itvmap_sametask_only.swap(task->itvmap_common);

	// TODO: Check if we can avoid this copy
	itvmap_t<acc_entry_t> itvmap_unmatched = task->itvmap_sametask_only;

	itvmap_t<mtc_ad_entry_t> itvmap_matched_writeonly;

	// Check for release dependencies
	for (dep_t *release_dep : task->release_deps) {
		itvmap_combine_intersection_mtc(
			&task->itvmap_sametask_only, &release_dep->itvmap,
			acc_match_dep,
			&res
		);
	}

	for (auto &it : res) {
		mtc_ad_entry_t *mtc_entry = &it.second;
		acc_entry_t *acc_entry = &mtc_entry->entry_a;
		dep_entry_t *dep_entry = &mtc_entry->entry_b;

		// itvmap_insert(
		// 	itvmap_new_mtc(
		// 		mtc_entry->itv.low, mtc_entry->itv.high,
		// 			MATCH_TYPE_YES_ACC_RELEASE_STRONG_DEP,
		// 		*acc_entry, dep_entry->dep),
		// 	&task->itvmap_mtc_acc_dep);
		mtc_adep_entry_t new_mtc_entry = itvmap_new_mtc(
			mtc_entry->itv.low, mtc_entry->itv.high,
				MATCH_TYPE_YES_ACC_RELEASE_STRONG_DEP,
			*acc_entry, dep_entry->dep);
		insert_match(
			&new_mtc_entry,
			&task->itvmap_mtc_acc_dep);

		acc_entry_t newacc = *acc_entry;
		newacc.itv.low = mtc_entry->itv.low;
		newacc.itv.high = mtc_entry->itv.high;

		// Accesses that match with release deps must be ignored
		// from now on, because it means that they are accessing
		// a region that is no longer valid
		itvmap_remove_fragmented(
			newacc, &task->itvmap_sametask_only,
			acc_touch_and_are_equal);

		itvmap_remove_fragmented(
			newacc, &itvmap_unmatched,
			acc_touch_and_are_equal);
	}

	res.clear();

	// Check for normal weak dependencies
	for (dep_t *normal_weak_dep : task->normal_deps) {
		if ((normal_weak_dep->mode & DEP_WEAK) == 0) {
			continue;
		}

		itvmap_combine_intersection_mtc(
			&task->itvmap_sametask_only, &normal_weak_dep->itvmap,
			acc_match_dep,
			&res
		);
	}

	for (auto &it : res) {
		mtc_ad_entry_t *mtc_entry = &it.second;
		acc_entry_t *acc_entry = &mtc_entry->entry_a;
		dep_entry_t *dep_entry = &mtc_entry->entry_b;

		// itvmap_insert(
		// 	itvmap_new_mtc(
		// 		mtc_entry->itv.low, mtc_entry->itv.high,
		// 			MATCH_TYPE_YES_ACC_NORMAL_WEAK_DEP,
		// 		*acc_entry, dep_entry->dep),
		// 	&task->itvmap_mtc_acc_dep);
		mtc_adep_entry_t new_mtc_entry = itvmap_new_mtc(
			mtc_entry->itv.low, mtc_entry->itv.high,
				MATCH_TYPE_YES_ACC_NORMAL_WEAK_DEP,
			*acc_entry, dep_entry->dep);
		insert_match(
			&new_mtc_entry,
			&task->itvmap_mtc_acc_dep);

		acc_entry_t newacc = *acc_entry;
		newacc.itv.low = mtc_entry->itv.low;
		newacc.itv.high = mtc_entry->itv.high;

		// NOTE: We don't mark the access as matched because it might
		// still be that some other strong dependency overlaps with
		// this one and the access hits
		// TODO: Detect strong deps that overlap wake deps in the
		// same tasks?
		itvmap_remove_fragmented(
			newacc, &itvmap_unmatched,
			acc_touch_and_are_equal);
	}

	res.clear();

	/*
	 * Checks if a normal, strong dependency is unmatched. This is
	 * only meaningful at the end of the current task.
	 * Also, every time a dependency is satisfied, it is removed
	 * from the set of unmatched dependencies.
	 * The next check is that of accesses that fall within the extent
	 * of a dependency, but not inside any fragment (SECTION match).
	 * These matches are reported.
	 * The next check is that of accesses that fall within the extent
	 * of a dependency and inside a fragment, but the access mode
	 * is not matching (ADDRESS match). These matches are reported.
	 * Finally, all the unmatched accesses are reported.
	 */

	// Check normal strong dependencies
	for (dep_t *normal_strong_dep : task->normal_deps) {
		if ((normal_strong_dep->mode & DEP_WEAK)) {
			continue;
		}

		itvmap_combine_intersection_mtc(
			&task->itvmap_sametask_only, &normal_strong_dep->itvmap,
			acc_match_dep,
			&res
		);
	}

	for (auto &it : res) {
		mtc_ad_entry_t *mtc_entry = &it.second;
		acc_entry_t *acc_entry = &mtc_entry->entry_a;
		dep_entry_t *dep_entry = &mtc_entry->entry_b;

		// itvmap_insert(
		// 	itvmap_new_mtc(
		// 		mtc_entry->itv.low, mtc_entry->itv.high,
		// 			MATCH_TYPE_YES_ACC_NORMAL_STRONG_DEP,
		// 		*acc_entry, dep_entry->dep),
		// 	&task->itvmap_mtc_acc_dep);
		mtc_adep_entry_t new_mtc_entry = itvmap_new_mtc(
			mtc_entry->itv.low, mtc_entry->itv.high,
				MATCH_TYPE_YES_ACC_NORMAL_STRONG_DEP,
			*acc_entry, dep_entry->dep);
		insert_match(
			&new_mtc_entry,
			&task->itvmap_mtc_acc_dep);

		acc_entry_t newacc = *acc_entry;
		newacc.itv.low = mtc_entry->itv.low;
		newacc.itv.high = mtc_entry->itv.high;

		// Remove the access from the unmatched set
		itvmap_remove_fragmented(
			newacc, &itvmap_unmatched,
			acc_touch_and_are_equal);

		if (acc_entry->itv.mode == ITV_MODE_WRITE
			&& dep_entry->dep->mode & DEP_WRITE
			&& !(dep_entry->dep->mode & DEP_READ)) {
			itvmap_insert(
				*mtc_entry, &itvmap_matched_writeonly);
		}

		// NOTE: dep_intersect_same_mode_not_NONE useless, possibly
		// only slightly faster

		// Remove the dependency from the unmatched set
		if (acc_entry->itv.mode == ITV_MODE_ALLOC || acc_entry->itv.mode == ITV_MODE_FREE) {
			// ALLOC and FREE accesses contribute twice because are treated like
			// READ-WRITE accesses. Having this special case is not ideal, a better
			// approach would be that of admitting READ-WRITE fragments (this would
			// require rewriting much of the interval maps logic, so let's leave it
			// to the future.)
			itvmap_remove_fragmented(
				itvmap_new_dep(
					ITV_MODE_READ, mtc_entry->itv.low, mtc_entry->itv.high, dep_entry->dep),
				&dep_entry->dep->itvmap_unmatched,
				// dep_intersect_same_mode_not_NONE);
				dep_intersect_and_same_mode);
			itvmap_remove_fragmented(
				itvmap_new_dep(
					ITV_MODE_WRITE, mtc_entry->itv.low, mtc_entry->itv.high, dep_entry->dep),
				&dep_entry->dep->itvmap_unmatched,
				// dep_intersect_same_mode_not_NONE);
				dep_intersect_and_same_mode);
		}
		else {
			itvmap_remove_fragmented(
				itvmap_new_dep(
					acc_entry->itv.mode, mtc_entry->itv.low, mtc_entry->itv.high, dep_entry->dep),
				&dep_entry->dep->itvmap_unmatched,
				// dep_intersect_same_mode_not_NONE);
				dep_intersect_and_same_mode);
		}
	}

	if (config.aggregation_level == config.AGGREGATE_NONE) {
		// Check for normal dependencies - READ AFTER WRITE
		for (dep_t *normal_strong_dep : task->normal_deps) {
			if ((normal_strong_dep->mode & DEP_WEAK)) {
				continue;
			}

			itvmap_combine_intersection_mtc(
				&itvmap_unmatched, &itvmap_matched_writeonly,
				acc_match_RaW,
				&res_raw
			);
		}

		for (auto &it : res_raw) {
			mtc_entry_t<acc_entry_t, mtc_ad_entry_t> *mtc_entry = &it.second;
			acc_entry_t *acc_entry = &mtc_entry->entry_a;
			dep_entry_t *dep_entry = &mtc_entry->entry_b.entry_b;

			// itvmap_insert(
			// 	itvmap_new_mtc(
			// 		acc_entry->itv.low, acc_entry->itv.high,
			// 			MATCH_TYPE_YES_READ_ACC_WRITE_NORMAL_STRONG_DEP,
			// 		*acc_entry, dep_entry->dep),
			// 	&task->itvmap_mtc_acc_dep);
			mtc_adep_entry_t new_mtc_entry = itvmap_new_mtc(
				acc_entry->itv.low, acc_entry->itv.high,
					MATCH_TYPE_YES_READ_ACC_WRITE_NORMAL_STRONG_DEP,
				*acc_entry, dep_entry->dep);
			insert_match(
				&new_mtc_entry,
				&task->itvmap_mtc_acc_dep);

			acc_entry_t newacc = *acc_entry;
			newacc.itv.low = mtc_entry->itv.low;
			newacc.itv.high = mtc_entry->itv.high;

			itvmap_remove_fragmented(
				newacc, &itvmap_unmatched,
				acc_touch_and_are_equal);
		}
	}

	// (5.) Update the original unmatched task access map
	task->itvmap_sametask_only.swap(itvmap_unmatched);

	// (6.) Check for unmatched task accesses
	for (auto &i : task->itvmap_sametask_only) {
		acc_entry_t *entry = &i.second;

		// TODO: Re-insert WRONG SECTION MATCH

		// itvmap_insert(
		// 	itvmap_new_mtc(
		// 		entry->itv.low, entry->itv.high,
		// 			MATCH_TYPE_NO_ACC_NORMAL_STRONG_DEP,
		// 		*entry, (dep_t *)NULL),
		// 	&task->itvmap_mtc_acc_dep);
		mtc_adep_entry_t new_mtc_entry = itvmap_new_mtc(
			entry->itv.low, entry->itv.high,
				MATCH_TYPE_NO_ACC_NORMAL_STRONG_DEP,
			*entry, (dep_t *)NULL);
		insert_match(
			&new_mtc_entry,
			&task->itvmap_mtc_acc_dep);
	}

	task->itvmap_sametask_only.clear();
	task->itvmap_common.clear();
}


/**
 * READ task->itvmap_mayrace
 * READ dep_child->itvmap_active:
 *   Possible conflicts:
 *     - REMOVE dep_sibling->itvmap_active in 'match_child_deps_with_sibling_deps'
 *       [NO] Executes via the same parent task at different times
 *     - CLEAR dep_child->itvmap_active in 'task_close_concurrent_region'
 *       [NO] Executes via the same parent task at different times
 * INSERT itvmap_mtc_acc_dep
 * REMOVE task->itvmap_mayrace
 */
/**
 * Intersects the accesses which may race in the current task
 * with the set of active dependencies of each child task,
 * starting from the first child task created after the last
 * full taskwait. For very match, we have a possible data race.
 * Before terminating, the set of active dependencies is erased
 * for each child task (we are executing a full taskwait), and
 * the set of accesses that can race follows the same fate.
 * We also enter a new race-free zone, until a new child task
 * is created.
 */
VOID match_acc_with_child_deps(task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	itvmap_t<mtc_ad_entry_t> res;

	itvmap_combine_intersection_mtc(
		&task->itvmap_childtask_only, &task->itvmap_dep_children,
		acc_match_child_dep,
		&res);

	itvmap_combine_intersection_mtc(
		&task->itvmap_common, &task->itvmap_dep_children,
		acc_match_child_dep,
		&res);

	for (auto &it : res) {
		mtc_ad_entry_t *mtc_entry = &it.second;
		acc_entry_t *acc_entry = &mtc_entry->entry_a;
		dep_entry_t *dep_entry = &mtc_entry->entry_b;

		if (acc_entry->itv.mode == ITV_MODE_READ && dep_entry->itv.mode == ITV_MODE_READ) {
			continue;
		}

		if (acc_entry->itv.mode == ITV_MODE_ALLOC || acc_entry->itv.mode == ITV_MODE_FREE) {
			// An ALLOC or FREE access could involve an area much greater than the
			// dependency (e.g, when we free a whole stack frame), so the interval
			// we save is that of the dependency rather than the access
			// (in the 'else' branch is the opposite cause we don't have this problem)
			// itvmap_insert(
			// 	itvmap_new_mtc(
			// 		// dep_entry->itv.low, dep_entry->itv.high,
			// 		mtc_entry->itv.low, mtc_entry->itv.high,
			// 			MATCH_TYPE_YES_ACC_NORMAL_DEP_CHILD,
			// 		*acc_entry, dep_entry->dep),
			// 	&task->itvmap_mtc_acc_dep);
			mtc_adep_entry_t new_mtc_entry = itvmap_new_mtc(
				// dep_entry->itv.low, dep_entry->itv.high,
				mtc_entry->itv.low, mtc_entry->itv.high,
					MATCH_TYPE_YES_ACC_NORMAL_DEP_CHILD,
				*acc_entry, dep_entry->dep);
			insert_match(
				&new_mtc_entry,
				&task->itvmap_mtc_acc_dep);
		}
		else {
			// itvmap_insert(
			// 	itvmap_new_mtc(
			// 		// acc_entry->itv.low, acc_entry->itv.high,
			// 		mtc_entry->itv.low, mtc_entry->itv.high,
			// 			MATCH_TYPE_YES_ACC_NORMAL_DEP_CHILD,
			// 		*acc_entry, dep_entry->dep),
			// 	&task->itvmap_mtc_acc_dep);
			mtc_adep_entry_t new_mtc_entry = itvmap_new_mtc(
				// acc_entry->itv.low, acc_entry->itv.high,
				mtc_entry->itv.low, mtc_entry->itv.high,
					MATCH_TYPE_YES_ACC_NORMAL_DEP_CHILD,
				*acc_entry, dep_entry->dep);
			insert_match(
				&new_mtc_entry,
				&task->itvmap_mtc_acc_dep);
		}

		// acc_entry_t newacc = *acc_entry;
		// newacc.itv.low = mtc_entry->itv.low;
		// newacc.itv.high = mtc_entry->itv.high;

		// NOTE: acc_same_address_same_mode seems wrong

		// itvmap_remove_fragmented(
		// 	newacc, &task->itvmap_mayrace,
		// 	// acc_same_address_same_mode);
		// 	acc_touch_and_are_equal);
	}

	task->itvmap_childtask_only.clear();
}


/**
 * READ dep_release->itvmap
 * READ dep_other_normal->itvmap_unreleased
 * REMOVE dep_other_normal->itvmap_unreleased
 * REMOVE dep_release->unmatched
 * READ dep_release->itvmap_unmatched
 * INSERT task->itvmap_mtc_dep_dep
 */
/**
 * The set of unreleased normal dependencies get shrink according
 * to such release dependencies.
 * Also, the set of unmatched release dependencies get shrink to
 * check if some of them try to release memory area that were
 * never registered in the first place.
 */
void match_release_dep_with_normal_deps(dep_t *dep_release, task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	itvmap_t<mtc_dd_entry_t> res;

	// Step 1: Combine the RELEASE dep with all normal strong deps
	for (dep_t *dep_other_normal : task->normal_deps) {
		if (dep_other_normal->mode & DEP_WEAK) {
			// TODO: At some point, it must be possible to release
			// weak dependencies
			continue;
		}

		// FIXME: Non posso usare unreleased perché dentro task_register_dep
		// dovrei ripetere il processo per ciascuna mappa
		itvmap_combine_intersection_mtc(
			// &dep_release->itvmap, &dep_other_normal->itvmap_unreleased,
			&dep_release->itvmap, &dep_other_normal->itvmap,
			dep_match_temporal,
			&res
		);
	}

	// Step 2: Process all found fragments
	for (auto &it : res) {
		mtc_dd_entry_t *mtc_entry = &it.second;
		// dep_entry_t *entry_a = &mtc_entry->entry_a;
		dep_entry_t *entry_b = &mtc_entry->entry_b;

		// Some fragments are marked as expired in the original
		// map because of this release dep
		dep_entry_t newentry = itvmap_new_dep(
			entry_b->itv.mode, mtc_entry->itv.low, mtc_entry->itv.high,
			entry_b->dep
		);

		newentry.start_time = entry_b->start_time;
		newentry.end_time = task->time;

		// Release deps matched by normal are eliminated to represent
		// the fact that they matched (see next for-loop)
		itvmap_remove_fragmented(
			newentry, &dep_release->itvmap_unmatched,
			dep_intersect_and_same_mode);

		// Normal deps matched by release are eliminated because
		// release deps conceptually "remove" normal deps.
		// itvmap_remove_fragmented(
		// 	newentry, &dep_other_normal->itvmap_unreleased,
		// 	dep_intersect_same_mode);

		// Some fragments are marked as expired
		itvmap_insert_fragmented(
			newentry, &entry_b->dep->itvmap,
			dep_intersect_and_same_mode);
	}

	// Step 3: The remaining fragments from the RELEASE dep don't
	// target any known normal strong dep, report this
	for (auto &it : dep_release->itvmap_unmatched) {
		dep_entry_t *entry = &it.second;

		// itvmap_insert(
		// 	itvmap_new_mtc(
		// 		entry->itv.low, entry->itv.high,
		// 			MATCH_TYPE_NO_RELEASE_STRONG_DEP_NORMAL_STRONG_DEP,
		// 		*entry, (dep_t *)NULL),
		// 	&task->itvmap_mtc_dep_dep);
		mtc_ddep_entry_t new_mtc_entry = itvmap_new_mtc(
			entry->itv.low, entry->itv.high,
				MATCH_TYPE_NO_RELEASE_STRONG_DEP_NORMAL_STRONG_DEP,
			*entry, (dep_t *)NULL);
		insert_match(
			&new_mtc_entry,
			&task->itvmap_mtc_dep_dep);
	}
}


/**
 * READ dep_parent->itvmap
 * READ dep_normal->itvmap
 * REMOVE dep_parent_normal_weak->itvmap_unmatched
 *   Possible conflicts:
 *     - READ/REMOVE normal_strong_dep->itvmap_unmatched in 'match_acc_with_deps'
 *       [NO] Executes via the same parent task at different times
 *     - READ dep_release->itvmap_unmatched in 'match_release_dep_with_normal_deps'
 *       [NO] Executes via the same parent task at different times and operates on different maps
 * INSERT task->itvmap_mtc_dep_dep
 */
/**
 * Check if a weak of strong dependency in the current task
 * is matched by a weak or strong dependency in the parent task.
 * If not, this fact is reported.
 */
VOID match_child_dep_with_parent_deps(dep_t *dep_normal, task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	itvmap_t<mtc_dd_entry_t> res;

	itvmap_t<dep_entry_t> itvmap_unmatched_parent = dep_normal->itvmap;

	// Step 1: Intersect child dep with parent deps
	// Both weak and strong deps in the parent satisfy normal
	// deps in child tasks...
	for (dep_t *dep_parent_normal : task->parent->normal_deps) {
		itvmap_combine_intersection_mtc(
			// &dep_parent_normal->itvmap_unreleased, &dep_normal->itvmap,
			&dep_parent_normal->itvmap, &dep_normal->itvmap,
			dep_match,
			&res
		);
	}

	// Step 2: Remove all intersecting fragments from the unmatched
	// set of the parent dep
	for (auto &it : res) {
		mtc_dd_entry_t *mtc_entry = &it.second;
		dep_entry_t *entry_a = &mtc_entry->entry_a;
		// dep_entry_t *entry_b = &mtc_entry->entry_b;

		dep_entry_t newentry = itvmap_new_dep(
			entry_a->itv.mode, mtc_entry->itv.low, mtc_entry->itv.high,
			entry_a->dep
		);

		if (entry_a->dep->mode & DEP_WEAK) {
			itvmap_remove_fragmented(
				newentry, &entry_a->dep->itvmap_unmatched,
				dep_intersect_and_same_mode);
				// deps_same_address_and_mode);
		}
		else {
			// The semantics for normal strong deps is different:
			// we cannot mark them as matched because this requires
			// at least one concrete access to hit
		}

		itvmap_remove_fragmented(
			newentry, &itvmap_unmatched_parent,
			dep_intersect_and_same_mode);
			// deps_same_address_and_mode);
	}

	// Step 3: Generate report
	for (auto &it : itvmap_unmatched_parent) {
		dep_entry_t *entry = &it.second;

		if (entry->itv.mode <= ITV_MODE_IGNORE) {
			continue;
		}

		// itvmap_insert(
		// 	itvmap_new_mtc(
		// 		entry->itv.low, entry->itv.high,
		// 			MATCH_TYPE_NO_NORMAL_DEP_PARENT_NORMAL_DEP_CHILD,
		// 		*entry, (dep_t *)NULL),
		// 	&task->itvmap_mtc_dep_dep);
		mtc_ddep_entry_t new_mtc_entry = itvmap_new_mtc(
			entry->itv.low, entry->itv.high,
				MATCH_TYPE_NO_NORMAL_DEP_PARENT_NORMAL_DEP_CHILD,
			*entry, (dep_t *)NULL);
		insert_match(
			&new_mtc_entry,
			&task->itvmap_mtc_dep_dep);
	}
}


/**
 * READ dep->itvmap
 * REMOVE dep_sibling->itvmap_active:
 *   Possible conflicts:
 *     - CLEAR dep_child->itvmap_active in 'task_close_concurrent_region'
 *       [NO] Executes via the same parent task at different times
 *     - READ dep_child->itvmap_active in 'match_acc_with_child_deps'
 *       [NO] Executes via the same parent task at different times
 */
/**
 * For every dependency in the current if0 task, it gets compared
 * with the dependency of every past sibling task.
 * The comparison check for fragments on the same region and
 * opposite access modes.
 * For example, if there is a sibling task with an out(a) and the
 * current if0 task has a dependency in(a), then we have a match.
 * For every match, the set of active dependencies of the sibling
 * task shrink.
 */
VOID match_child_deps_with_sibling_deps(task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	itvmap_t<mtc_dd_entry_t> res;
	itvmap_t<dep_entry_t> itvmap_matched_parent;

	for (dep_t *dep_normal : task->normal_deps) {
		itvmap_combine_intersection_mtc(
			&dep_normal->itvmap, &task->parent->itvmap_dep_children,
			dep_match_diff_mode,
			&res);
	}

	// FIXME: 'dep_match_diff_mode' should discard those deps
	// that were already targeted by a previous taskwait

	for (auto &it_a : res) {
		mtc_dd_entry_t *mtc_entry = &it_a.second;
		// dep_entry_t *entry_a = &mtc_entry->entry_a;
		dep_entry_t *entry_b = &mtc_entry->entry_b;

		if (entry_b->dep->task == task) {
			continue;
		}

		// if (entry_b->end_time <= task->parent->time) {
		// 	continue;
		// }

		// printf("Dep %s %s from task %u\n",
		// 	entry_b->dep->name,
		// 	itv_mode_str(entry_b->itv.mode),
		// 	entry_b->dep->task->id);

		for (auto &it_b : task->parent->itvmap_dep_children) {
			dep_entry_t *entry_c = &it_b.second;

			if (entry_c->end_time < task->parent->time) {
				continue;
			}

			if (entry_b->dep->task == entry_c->dep->task) {
				entry_c->end_time = task->parent->time;
			}
		}
	}
}


/**
 * READ dep->itvmap_unmatched
 * INSERT task->itvmap_mtc_dep_dep
 */
/*
 * Checks if a strong dependency was matched by any access.
 * This is only meaningful at the end of the current task.
 */
void check_unmatched_normal_strong_deps(task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	for (dep_t *normal_strong_dep : task->normal_deps) {
		if (normal_strong_dep->mode & DEP_FAKE) {
			continue;
		}

		if ((normal_strong_dep->mode & DEP_WEAK)) {
			continue;
		}

		for (auto &it : normal_strong_dep->itvmap_unmatched) {
			dep_entry_t *entry = &it.second;

			if ((entry->dep->mode & DEP_WRITE) && !((entry->dep->mode & DEP_READ))) {
				// An 'out' dependency fragment that is not satisfied
				// will be treated like a error, not a notice
				// itvmap_insert(
				// 	itvmap_new_mtc(
				// 		entry->itv.low, entry->itv.high,
				// 			MATCH_TYPE_NO_NORMAL_STRONG_OUT_DEP_ACC,
				// 		*entry, (dep_t *)NULL),
				// 	&task->itvmap_mtc_dep_dep);
				mtc_ddep_entry_t new_mtc_entry = itvmap_new_mtc(
					entry->itv.low, entry->itv.high,
						MATCH_TYPE_NO_NORMAL_STRONG_OUT_DEP_ACC,
					*entry, (dep_t *)NULL);
				insert_match(
					&new_mtc_entry,
					&task->itvmap_mtc_dep_dep);
			}
			else {
				// itvmap_insert(
				// 	itvmap_new_mtc(
				// 		entry->itv.low, entry->itv.high,
				// 			MATCH_TYPE_NO_NORMAL_STRONG_DEP_ACC,
				// 		*entry, (dep_t *)NULL),
				// 	&task->itvmap_mtc_dep_dep);
				mtc_ddep_entry_t new_mtc_entry = itvmap_new_mtc(
					entry->itv.low, entry->itv.high,
						MATCH_TYPE_NO_NORMAL_STRONG_DEP_ACC,
					*entry, (dep_t *)NULL);
				insert_match(
					&new_mtc_entry,
					&task->itvmap_mtc_dep_dep);
			}
		}
	}
}


/**
 * READ dep->itvmap_unmatched
 * INSERT task->itvmap_mtc_dep_dep
 */
/*
 * Checks if a weak dependency is matched by any weak or non-weak
 * dependency in child tasks. This is only meaningful at the end
 * of the current task.
 */
void check_unmatched_normal_weak_deps(task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	for (dep_t *normal_strong_dep : task->normal_deps) {
		if ((normal_strong_dep->mode & DEP_WEAK) == 0) {
			continue;
		}

		for (auto &it : normal_strong_dep->itvmap_unmatched) {
			dep_entry_t *entry = &it.second;

			// itvmap_insert(
			// 	itvmap_new_mtc(
			// 		entry->itv.low, entry->itv.high,
			// 			MATCH_TYPE_NO_NORMAL_DEP_CHILD_NORMAL_WEAK_DEP_PARENT,
			// 		*entry, (dep_t *)NULL),
			// 	&task->itvmap_mtc_dep_dep);
			mtc_ddep_entry_t new_mtc_entry = itvmap_new_mtc(
				entry->itv.low, entry->itv.high,
					MATCH_TYPE_NO_NORMAL_DEP_CHILD_NORMAL_WEAK_DEP_PARENT,
				*entry, (dep_t *)NULL);
			insert_match(
				&new_mtc_entry,
				&task->itvmap_mtc_dep_dep);
		}
	}
}


void promote_weak_deps_to_strong(task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	for (dep_t *normal_weak_dep : task->normal_deps) {
		if (!(normal_weak_dep->mode & DEP_WEAK)) {
			continue;
		}

		// This dep is not weak anymore...
		normal_weak_dep->mode = (dep_mode_t)(normal_weak_dep->mode & ~((dep_mode_t)DEP_WEAK));
	}
}


/**
 * CLEAR dep_child->itvmap_active
 *   Possible conflicts:
 *     - READ dep_child->itvmap_active in 'match_acc_with_child_deps'
 *       [NO] Executes via the same parent task at different times
 *     - REMOVE dep_sibling->itvmap_active in 'match_child_deps_with_sibling_deps'
 *       [NO] Executes via the same parent task at different times
 * CLEAR task->itvmap_mayrace
 */
void close_concurrent_region(task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	// We fragment dependencies based on fragment that have expired
	for (auto &it : task->itvmap_dep_children) {
		dep_entry_t *entry = &it.second;

		if (entry->end_time < task->time) {
			continue;
		}

		entry->end_time = task->time;
	}
}


void task_print_invocationpoint(task_data_t *task, unsigned int report_level, bool *has_reports) {
	if (*has_reports == false) {
		*has_reports = true;

		// report(report_level, task->thread,
		// config.easy_output ? NULL : task, "");
		report(report_level, task->thread,
			config.easy_output ? NULL : task,
			"In task %s:",
			task->invocationpoint);
	}
}


char *compute_error_hash(task_data_t *task, char *hash_str, size_t max_hash_length,
                         mtc_type_t mtc_entry_type, acc_entry_t *acc_entry, const char *line,
                         dep_entry_t *dep_entry, dep_t *dep) {
	if (!hash_str || max_hash_length == 0 || (!acc_entry && !dep_entry)) {
		return (char *)"{||}";
	}

	// MatchType, ITVMode, PC, InvPoint_1, Dep_1ID, InvPoint_2, Dep_2ID
	snprintf(hash_str, max_hash_length, "{|%u|%u|%p|%s|%u|%s|%u|}",
		mtc_entry_type,
		acc_entry ? acc_entry->itv.mode : dep_entry->itv.mode,
		acc_entry ? (void *)acc_entry->pc : NULL,
		acc_entry ? line : dep_entry->dep->task->invocationpoint,
		acc_entry ? 0 : dep_entry->dep->id,
		task->invocationpoint,
		dep ? dep->id : 0
	);

	return hash_str;
}


void task_report_matches(task_data_t *task) {
	expect(config.without_analysis == false,
		"This analysis function should not be called.");

	PIN_GetLock(&app.locks.stdout_channel, 1);

	bool has_reports = false;

	char cmd[MAX_COMMAND_LENGTH];
	int32_t status;
	unsigned long channels[3] = { STDIN_FD, task->thread->write_fd, STDERR_FD };

	size_t count;
	char file_line_info[2048];

	itvmap_t<mtc_entry_t <acc_entry_t, dep_t *>> itvmap_mtc_acc_dep, *itvmap_mtc_acc_dep_ptr;

	itvmap_mtc_acc_dep_ptr = &task->itvmap_mtc_acc_dep;

	// char error_hash[512];

	/* if (config.aggregation_level == config.AGGREGATE_PC) {
		itvmap_mtc_acc_dep_ptr = &itvmap_mtc_acc_dep;
		// itvmap_mtc_acc_dep.clear();

		for (auto &it : task->itvmap_mtc_acc_dep) {
			mtc_entry_t<acc_entry_t, dep_t *> *entry = &it.second;

			itvmap_insert_aggregated(
				*entry, &itvmap_mtc_acc_dep, mtc_acc_touch
			);
		}
	}
	else */ if (config.easy_output) {
		PIN_GetLock(&app.locks.itvmap_mtc_acc_dep, 1);
		if (app.itvmap_mtc_acc_dep.find(task->invocationpoint) == app.itvmap_mtc_acc_dep.end()) {
			app.itvmap_mtc_acc_dep[task->invocationpoint] = itvmap_t<mtc_entry_t<acc_entry_t, dep_t *>>();
		}
		PIN_ReleaseLock(&app.locks.itvmap_mtc_acc_dep);

		PIN_GetLock(&app.locks.itvmap_mtc_dep_dep, 1);
		if (app.itvmap_mtc_dep_dep.find(task->invocationpoint) == app.itvmap_mtc_dep_dep.end()) {
			app.itvmap_mtc_dep_dep[task->invocationpoint] = itvmap_t<mtc_entry_t<dep_entry_t, dep_t *>>();
		}
		PIN_ReleaseLock(&app.locks.itvmap_mtc_dep_dep);
	}

	for (auto &it : *itvmap_mtc_acc_dep_ptr) {
		mtc_entry_t<acc_entry_t, dep_t *> *entry = &it.second;

		if ((unsigned int)entry->type < (unsigned int)config.min_report_level) {
			continue;
		}

		if (config.easy_output) {
			bool proceed = false;

			PIN_GetLock(&app.locks.itvmap_mtc_acc_dep, 1);
			proceed = itvmap_insert_conditional(
				*entry, &app.itvmap_mtc_acc_dep[task->invocationpoint], mtc_already_exist
			);
			PIN_ReleaseLock(&app.locks.itvmap_mtc_acc_dep);

			if (!proceed) {
				continue;
			}
		}

		acc_entry_t *acc = &entry->entry_a;
		dep_t *dep = entry->entry_b;

		img_t *img = image_find_by_id(acc->imgid);

		void *pc;

		expect(img,
			"Image %u must exist.", acc->imgid);

		if (img->has_final_vmas) {
			pc = (void *)(acc->pc);
		} else {
			pc = (void *)(acc->pc - img->extent_low);
		}

		snprintf(cmd, MAX_COMMAND_LENGTH, "addr2line -e %s %p",
			img->name, pc);

		debug(5, NULL, NULL,
			"Invoking `%s`", cmd);

		err_t err = utils_spawn_process(cmd, true, &status, channels);

		if (err == ERR_CANT_SPAWN_PROC) {
			error(task->thread, task,
				"Unable to spawn process to execute command '%s'.", cmd);
		}
		else if (err == ERR_CANT_WAIT_PROC) {
			error(task->thread, task,
				"Unable to wait for termination of command '%s'.", cmd);
		}
		else if (status != 0) {
			// FIXME: Why does it happen sometimes?
			//error("Command '%s' exited with errors.", cmd);
		}

		if (utils_read_fd(task->thread->read_fd, file_line_info, MAX_OUTPUT_LENGTH, &count)) {
			error(task->thread, task,
				"Unable to read %u bytes from pipe",
				MAX_OUTPUT_LENGTH);
		}

		file_line_info[strcspn(file_line_info, " ")] = '\0';
		file_line_info[strcspn(file_line_info, "\n")] = '\0';

		// INT32 column, line;
		// string filename;

		// PIN_LockClient();
		// PIN_GetSourceLocation((ADDRINT) pc, &column, &line, &filename);
		// PIN_UnlockClient();

		// snprintf(file_line_info, 2048, "%s:%u:%u", filename.c_str(), line, column);

		const char *opcode = OPCODE_StringShort(acc->opcode).c_str();

		opaque_obj_t obj;

		opaque_obj_lookup(task, &obj, acc->itv.low, acc->itv.high);

		// char *hash_str = compute_error_hash(task, error_hash, sizeof(error_hash),
		// 	entry->type, acc, basename(file_line_info), NULL, dep);

		switch (entry->type) {
			case MATCH_TYPE_YES_ACC_NORMAL_STRONG_DEP:
				debug(3, task->thread,
					config.easy_output ? NULL : task,
					"%s %s access %-12.12s to object <'%s' (%p[%lu:%lu])> in <'%s' (+%p)>"
					" MATCHES %s dependency '%s %s'",
					// hash_str,
					" ",
					itv_mode_str(acc->itv.mode), opcode,
					dep->name, dep->extent_low,
					acc->itv.low - dep->extent_low,
					acc->itv.high - dep->extent_low,
					// basename(img->name), basename(strchr(file_line_info, ':')),
					basename(file_line_info),
					acc->pc - img->extent_low,
					dep_type_str(dep->type, dep->mode),
					dep_mode_str(dep->mode),
					dep->name);
			break;

			case MATCH_TYPE_YES_READ_ACC_WRITE_NORMAL_STRONG_DEP:
				debug(3, task->thread,
					config.easy_output ? NULL : task,
					"%s %s access %-12.12s to object <'%s' (%p[%lu:%lu])> in <'%s' (+%p)>"
					// " INDIRECTLY MATCHES PREVIOUSLY SATISFIED %s dependency '%s %s'",
					" IS A READ-AFTER-WRITE",
					// hash_str,
					" ",
					itv_mode_str(acc->itv.mode), opcode,
					dep->name, dep->extent_low,
					acc->itv.low - dep->extent_low,
					acc->itv.high - dep->extent_low,
					basename(file_line_info),
					acc->pc - img->extent_low,
					dep_type_str(dep->type, dep->mode),
					dep_mode_str(dep->mode),
					dep->name);
			break;

			case MATCH_TYPE_NO_ACC_NORMAL_STRONG_DEP:
				task_print_invocationpoint(task, config.REPORT_WARNING, &has_reports);

				report(config.REPORT_WARNING, task->thread,
					config.easy_output ? NULL : task,
					"%s %s access %-12.12s to object <'%s' (%p[%lu:%lu])> in <'%s' (+%p)>"
					" DOES NOT MATCH any dependency",
					// hash_str,
					" ",
					itv_mode_str(acc->itv.mode), opcode,
					// lookup_name_from_addr(task, acc->itv.low, acc->itv.high), acc->itv.lowptr,
					// acc->itv.low - acc->itv.low,
					// acc->itv.high - acc->itv.low,
					obj.name, (void *) obj.extent_low,
					acc->itv.low - obj.extent_low,
					acc->itv.high - obj.extent_low,
					basename(file_line_info),
					acc->pc - img->extent_low);
			break;

			case MATCH_TYPE_YES_ACC_NORMAL_DEP_CHILD:
				task_print_invocationpoint(task, config.REPORT_WARNING, &has_reports);

				report(config.REPORT_WARNING, task->thread,
					config.easy_output ? NULL : task,
					"%s %s access %-12.12s to object <'%s' (%p[%lu:%lu])> in <'%s' (+%p)>"
					" CAN RACE WITH %s dependency '%s %s' in child task %s",
					// hash_str,
					" ",
					itv_mode_str(acc->itv.mode), opcode,
					dep->name, dep->extent_low,
					// NOTE: For an explanation of why we use 'entry', see the ALLOC-FREE
					// comment inside 'match_acc_with_child_deps'
					entry->itv.low - dep->extent_low,
					entry->itv.high - dep->extent_low,
					basename(file_line_info),
					acc->pc - img->extent_low,
					dep_type_str(dep->type, dep->mode),
					dep_mode_str(dep->mode),
					dep->name,
					dep->task->invocationpoint);
			break;

			case MATCH_TYPE_YES_ACC_RELEASE_STRONG_DEP:
				task_print_invocationpoint(task, config.REPORT_WARNING, &has_reports);

				report(config.REPORT_WARNING, task->thread,
					config.easy_output ? NULL : task,
					"%s %s access %-12.12s to object <'%s' (%p[%lu:%lu])> in <'%s' (+%p)>"
					" MUST NOT MATCH %s dependency '%s %s'",
					// hash_str,
					" ",
					itv_mode_str(acc->itv.mode), opcode,
					// lookup_name_from_addr(task, acc->itv.low, acc->itv.high), dep->extent_low,
					// acc->itv.low - dep->extent_low,
					// acc->itv.high - dep->extent_low,
					obj.name, (void *) obj.extent_low,
					acc->itv.low - obj.extent_low,
					acc->itv.high - obj.extent_low,
					basename(file_line_info),
					acc->pc - img->extent_low,
					dep_type_str(dep->type, dep->mode),
					dep_mode_str(dep->mode),
					dep->name);
			break;

			case MATCH_TYPE_YES_ACC_NORMAL_WEAK_DEP:
				task_print_invocationpoint(task, config.REPORT_WARNING, &has_reports);

				report(config.REPORT_WARNING, task->thread,
					config.easy_output ? NULL : task,
					"%s %s access %-12.12s to object <'%s' (%p[%lu:%lu])> in <'%s' (+%p)>"
					" MUST NOT MATCH %s dependency '%s %s'",
					// hash_str,
					" ",
					itv_mode_str(acc->itv.mode), opcode,
					dep->name, dep->extent_low,
					acc->itv.low - dep->extent_low,
					acc->itv.high - dep->extent_low,
					basename(file_line_info),
					acc->pc - img->extent_low,
					dep_type_str(dep->type, dep->mode),
					dep_mode_str(dep->mode),
					dep->name);
			break;

			case MATCH_TYPE_YES_ACC_NORMAL_STRONG_DEP_MODE:
				task_print_invocationpoint(task, config.REPORT_WARNING, &has_reports);

				report(config.REPORT_WARNING, task->thread,
					config.easy_output ? NULL : task,
					"%s %s access %-12.12s to object <'%s' (%p[%lu:%lu])> in <'%s' (+%p)> "
					"DOES NOT MATCH ACCESSMODE in %s dependency '%s %s'",
					// hash_str,
					" ",
					itv_mode_str(acc->itv.mode), opcode,
					dep->name, dep->extent_low,
					acc->itv.low - dep->extent_low,
					acc->itv.high - dep->extent_low,
					basename(file_line_info),
					acc->pc - img->extent_low,
					dep_type_str(dep->type, dep->mode),
					dep_mode_str(dep->mode),
					dep->name);
			break;

			case MATCH_TYPE_YES_ACC_NORMAL_STRONG_DEP_SECTION:
				task_print_invocationpoint(task, config.REPORT_WARNING, &has_reports);

				report(config.REPORT_WARNING, task->thread,
					config.easy_output ? NULL : task,
					"%s %s access %-12.12s to object <'%s' (%p[%lu:%lu])> in <'%s' (+%p)> "
					"DOES NOT MATCH SECTION in %s dependency '%s %s'",
					// hash_str,
					" ",
					itv_mode_str(acc->itv.mode), opcode,
					dep->name, dep->extent_low,
					acc->itv.low - dep->extent_low,
					acc->itv.high - dep->extent_low,
					basename(file_line_info),
					acc->pc - img->extent_low,
					dep_type_str(dep->type, dep->mode),
					dep_mode_str(dep->mode),
					dep->name);
			break;

			default:
				error(task->thread, task,
					"Unknown match type.");
		}
	}

	for (auto &it : task->itvmap_mtc_dep_dep) {
		mtc_entry_t<dep_entry_t, dep_t *> *entry = &it.second;

		if ((unsigned int)entry->type < (unsigned int)config.min_report_level) {
			continue;
		}

		if (config.easy_output) {
			bool proceed = false;

			PIN_GetLock(&app.locks.itvmap_mtc_dep_dep, 1);
			proceed = itvmap_insert_conditional(
				*entry, &app.itvmap_mtc_dep_dep[task->invocationpoint], mtc_already_exist
			);
			PIN_ReleaseLock(&app.locks.itvmap_mtc_dep_dep);

			if (!proceed) {
				continue;
			}
		}

		dep_entry_t *dep = &entry->entry_a;

		// char *hash_str = compute_error_hash(task, error_hash, sizeof(error_hash),
		// 	entry->type, NULL, NULL, dep, dep->dep);

		switch (entry->type) {
			case MATCH_TYPE_NO_RELEASE_STRONG_DEP_NORMAL_STRONG_DEP:
				task_print_invocationpoint(task, config.REPORT_NOTICE, &has_reports);

				report(
					config.REPORT_NOTICE, task->thread,
					config.easy_output ? NULL : task,
					"%s NO NORMAL %s DEPENDENCY FOUND for %s dependency <'%s %s':%p[%lu:%lu]>",
					// hash_str,
					" ",
					itv_mode_str(dep->itv.mode),
					dep_type_str(dep->dep->type, dep->dep->mode),
					dep_mode_str(dep->dep->mode),
					dep->dep->name, dep->dep->extent_low,
					dep->itv.low - dep->dep->extent_low,
					dep->itv.high - dep->dep->extent_low);
			break;

			case MATCH_TYPE_NO_NORMAL_STRONG_OUT_DEP_ACC:
				task_print_invocationpoint(task, config.REPORT_WARNING, &has_reports);

				report(
					config.REPORT_WARNING, task->thread,
					config.easy_output ? NULL : task,
					"%s NO %s ACCESS FOUND for %s dependency <'%s %s':%p[%lu:%lu]>",
					// hash_str,
					" ",
					itv_mode_str(dep->itv.mode),
					dep_type_str(dep->dep->type, dep->dep->mode),
					dep_mode_str(dep->dep->mode),
					dep->dep->name, dep->dep->extent_low,
					dep->itv.low - dep->dep->extent_low,
					dep->itv.high - dep->dep->extent_low);
			break;

			case MATCH_TYPE_NO_NORMAL_STRONG_DEP_ACC:
				task_print_invocationpoint(task, config.REPORT_NOTICE, &has_reports);

				report(
					config.REPORT_NOTICE, task->thread,
					config.easy_output ? NULL : task,
					"%s NO %s ACCESS FOUND for %s dependency <'%s %s':%p[%lu:%lu]>",
					// hash_str,
					" ",
					itv_mode_str(dep->itv.mode),
					dep_type_str(dep->dep->type, dep->dep->mode),
					dep_mode_str(dep->dep->mode),
					dep->dep->name, dep->dep->extent_low,
					dep->itv.low - dep->dep->extent_low,
					dep->itv.high - dep->dep->extent_low);
			break;

			case MATCH_TYPE_NO_NORMAL_DEP_PARENT_NORMAL_DEP_CHILD:
				task_print_invocationpoint(task, config.REPORT_WARNING, &has_reports);

				report(
					config.REPORT_WARNING, task->thread,
					config.easy_output ? NULL : task,
					"%s NO MATCHING %s DEPS FOUND IN PARENT TASK for %s dependency <'%s %s':%p[%lu:%lu]>",
					// hash_str,
					" ",
					itv_mode_str(dep->itv.mode),
					dep_type_str(dep->dep->type, dep->dep->mode),
					dep_mode_str(dep->dep->mode),
					dep->dep->name, dep->dep->extent_low,
					dep->itv.low - dep->dep->extent_low,
					dep->itv.high - dep->dep->extent_low);
			break;

			case MATCH_TYPE_NO_NORMAL_DEP_CHILD_NORMAL_WEAK_DEP_PARENT:
				task_print_invocationpoint(task, config.REPORT_NOTICE, &has_reports);

				report(
					config.REPORT_NOTICE, task->thread,
					config.easy_output ? NULL : task,
					"%s NO MATCHING %s DEPS FOUND IN CHILD TASKS for %s dependency <'%s %s':%p[%lu:%lu]>",
					// hash_str,
					" ",
					itv_mode_str(dep->itv.mode),
					dep_type_str(dep->dep->type, dep->dep->mode),
					dep_mode_str(dep->dep->mode),
					dep->dep->name, dep->dep->extent_low,
					dep->itv.low - dep->dep->extent_low,
					dep->itv.high - dep->dep->extent_low);
			break;

			default:
				error(task->thread, task,
					"Unknown match type.");
		}
	}

	if (has_reports) {
		app.has_reports = true;
	}

	PIN_ReleaseLock(&app.locks.stdout_channel);
}


// bool deps_same_address_and_mode(const dep_entry_t *val, const dep_entry_t *other,
//                                 uint64_t low, uint64_t high,
//                                 const itvmap_t<dep_entry_t> *map) {
// 	// printf("Compare %s %p <%p + %lu> VS %s %p <%p + %lu> => "
// 	// 	"INTERSECT AT (%p + %lu)\n",
// 	// 	itv_mode_str(val->itv.mode), val->dep, val->itv.lowptr, val->itv.high - val->itv.low,
// 	// 	itv_mode_str(other->itv.mode), other->dep, other->itv.lowptr, other->itv.high - other->itv.low ,
// 	// 	(void *)low, high - low
// 	// );
// 	return (low < high && val->itv.mode == other->itv.mode);
// }


// bool dep_intersect_same_mode_not_NONE(const dep_entry_t *val, const dep_entry_t *other,
//                      uint64_t low, uint64_t high,
//                      const itvmap_t<dep_entry_t> *map) {
// 	// printf("Compare %s %p <%p + %lu> VS %s %p <%p + %lu> => "
// 	// 	"INTERSECT AT (%p + %lu)\n",
// 	// 	itv_mode_str(val->itv.mode), val->dep, val->itv.lowptr, val->itv.high - val->itv.low,
// 	// 	itv_mode_str(other->itv.mode), other->dep, other->itv.lowptr, other->itv.high - other->itv.low ,
// 	// 	(void *)low, high - low
// 	// );
// 	return low < high && other->itv.mode > ITV_MODE_NONE && val->itv.mode == other->itv.mode;
// }


// bool fragment_dep_if_2(const dep_entry_t *val, const dep_entry_t *other,
//                      uint64_t low, uint64_t high,
//                      const itvmap_t<dep_entry_t> *map) {
// 	return low < high && other->itv.mode > ITV_MODE_IGNORE;
// }


// bool acc_same_address_same_mode(
// 	const acc_entry_t *val, const acc_entry_t *other,
// 	uint64_t low, uint64_t high,
// 	const itvmap_t<acc_entry_t> *map
// ) {
// 	// printf("Compare %s <%p + %lu> VS %s <%p + %lu> => "
// 	// 	"INTERSECT AT (%p + %lu)\n",
// 	// 	itv_mode_str(val->itv.mode), val->itv.lowptr, val->itv.high - val->itv.low,
// 	// 	itv_mode_str(other->itv.mode), other->itv.lowptr, other->itv.high - other->itv.low ,
// 	// 	(void *)low, high - low
// 	// );
// 	return (low < high && other->itv.mode > ITV_MODE_NONE && val->itv.mode == other->itv.mode);
// }


// bool dep_are_same(const dep_entry_t *val, const dep_entry_t *other,
//                   uint64_t low, uint64_t high,
//                   const itvmap_t<dep_entry_t> *map) {
// 	return (val->itv.mode > ITV_MODE_IGNORE && val->itv.mode == other->itv.mode);
// }


// bool dep_unmatched_cond(const dep_entry_t *val_a, const acc_entry_t *val_b,
//                         uint64_t intersect_low, uint64_t intersect_high,
//                         uint64_t remainder_low, uint64_t remainder_high,
//                         const itvmap_t<dep_entry_t> *map_out) {
// 	return (val_b ? val_a->itv.mode == val_b->itv.mode : val_a->itv.mode > ITV_MODE_IGNORE);
// }


// bool acc_match_dep_1(
// 	const acc_entry_t *val_a, const dep_entry_t *val_b,
// 	uint64_t intersect_low, uint64_t intersect_high,
// 	uint64_t remainder_low, uint64_t remainder_high,
// 	const itvmap_t<mtc_ad_entry_t> *map_out
// ) {
// 	bool res = (
// 		val_b
// 		&& (
// 			val_a->itv.mode == ITV_MODE_ALLOC || val_a->itv.mode == ITV_MODE_FREE
// 			||
// 			val_a->itv.mode == val_b->itv.mode
// 		)
// 		&& val_a->time > val_b->start_time
// 	);
// 	// printf("Compare %s <%p + %lu> %p VS %s <%p + %lu> => "
// 	// 	"INTERSECT AT (%p + %lu), REMAINDER AT (%p + %lu) = %u\n",
// 	// 	itv_mode_str(val_a->itv.mode), val_a->itv.lowptr, val_a->itv.high - val_a->itv.low,
// 	// 	(void *)val_a->pc,
// 	// 	val_b ? itv_mode_str(val_b->itv.mode) : "-",
// 	// 	val_b ? val_b->itv.lowptr : 0x0,
// 	// 	val_b ? val_b->itv.high - val_b->itv.low : 0,
// 	// 	(void *)intersect_low, intersect_high - intersect_low,
// 	// 	(void *)remainder_low, intersect_low  - remainder_low,
// 	// 	res
// 	// );
// 	return res;
// }

// bool acc_matched_full_cond_child(const acc_entry_t *val_a, const dep_entry_t *val_b,
//                                  uint64_t intersect_low, uint64_t intersect_high,
//                                  uint64_t remainder_low, uint64_t remainder_high,
//                                  const itvmap_t<acc_entry_t> *map_out) {
// 	// printf("Compare %s <%p + %lu> VS %s <%p + %lu> => "
// 	// 	"INTERSECT AT (%p + %lu), REMAINDER AT (%p + %lu)\n",
// 	// 	itv_mode_str(val_a->itv.mode), val_a->itv.lowptr, val_a->itv.high - val_a->itv.low,
// 	// 	val_b ? itv_mode_str(val_b->itv.mode) : "-",
// 	// 	val_b ? val_b->itv.lowptr : 0x0,
// 	// 	val_b ? val_b->itv.high - val_b->itv.low : 0,
// 	// 	(void *)intersect_low, intersect_high - intersect_low,
// 	// 	(void *)remainder_low, intersect_low  - remainder_low
// 	// );
// 	return (val_b
// 		&& (
// 			// It is an ALLOC/FREE operation
// 			val_a->itv.mode == ITV_MODE_ALLOC || val_a->itv.mode == ITV_MODE_FREE
// 			||
// 			// It is a REAL/WRITE access which coincide with the dependency
// 			val_a->itv.mode == val_b->itv.mode
// 		)
// 	);
// }


// bool acc_matched_section_cond(const acc_entry_t *val_a, const dep_entry_t *val_b,
//                               uint64_t intersect_low, uint64_t intersect_high,
//                               uint64_t remainder_low, uint64_t remainder_high,
//                               const itvmap_t<acc_entry_t> *map_out) {
// 	// printf("Compare %s <%p + %lu> VS %s <%p + %lu> => "
// 	// 	"INTERSECT AT (%p + %lu), REMAINDER AT (%p + %lu)\n",
// 	// 	itv_mode_str(val_a->itv.mode), val_a->itv.lowptr, val_a->itv.high - val_a->itv.low,
// 	// 	val_b ? itv_mode_str(val_b->itv.mode) : "-",
// 	// 	val_b ? val_b->itv.lowptr : 0x0,
// 	// 	val_b ? val_b->itv.high - val_b->itv.low : 0,
// 	// 	(void *)intersect_low, intersect_high - intersect_low,
// 	// 	(void *)remainder_low, intersect_low  - remainder_low
// 	// );
// 	return (val_b && val_b->itv.mode == ITV_MODE_NONE);
// }


// bool acc_matched_address_cond(const acc_entry_t *val_a, const dep_entry_t *val_b,
//                               uint64_t intersect_low, uint64_t intersect_high,
//                               uint64_t remainder_low, uint64_t remainder_high,
//                               const itvmap_t<acc_entry_t> *map_out) {
// 	return (val_b && val_b->itv.mode > ITV_MODE_IGNORE && val_a->itv.mode != val_b->itv.mode);
// }


// bool deps_same_address_diff_mode(const dep_entry_t *val, const dep_entry_t *other,
//                                 uint64_t low, uint64_t high,
//                                 const itvmap_t<dep_entry_t> *map) {
// 	return (low < high && other->itv.mode > ITV_MODE_NONE && val->itv.mode != other->itv.mode);
// }
