/*
	This file is part of OmpSs-2 Linter and is licensed under
	the terms contained in the COPYING file.

	Copyright (C) 2019 Barcelona Supercomputing Center (BSC)
*/

template <typename entry_t>
using itvmap_process_pred_func_t =
	bool (*)(const entry_t *val, const entry_t *other,
	         uint64_t low, uint64_t high, const itvmap_t<entry_t> *map);


template <typename entry_t>
using itvmap_process_proc_func_t =
	bool (*)(entry_t *val, entry_t *other,
	         uint64_t low, uint64_t high,
	         itvmap_t<entry_t> *map, itvmap_it_t<entry_t> *it,
	         itvmap_t<entry_t> *insert_map, std::list<itvmap_it_t<entry_t>> *remove_map);


template <typename entry_a_t, typename entry_b_t>
using itvmap_combine_pred_func_t =
	bool (*)(const entry_a_t *val_a, const entry_b_t *val_b,
	         uint64_t intersect_low, uint64_t intersect_high,
	         uint64_t remainder_low, uint64_t remainder_high,
	         const itvmap_t<entry_a_t> *map_out);


template <typename entry_a_t, typename entry_b_t, typename entry_c_t>
using itvmap_combine_pred_func_2_t =
	bool (*)(const entry_a_t *val_a, const entry_b_t *val_b,
	         uint64_t intersect_low, uint64_t intersect_high,
	         uint64_t remainder_low, uint64_t remainder_high,
	         const itvmap_t<entry_c_t> *map_out);


template <typename entry_a_t, typename entry_b_t>
using itvmap_combine_proc_func_t =
	bool (*)(entry_a_t *val_a, const entry_b_t *val_b,
	         uint64_t intersect_low, uint64_t intersect_high,
	         uint64_t remainder_low, uint64_t remainder_high,
	         itvmap_t<entry_a_t> *map_out);


template <typename entry_a_t, typename entry_b_t, typename entry_c_t>
using itvmap_combine_proc_func_2_t =
	bool (*)(entry_a_t *val_a, const entry_b_t *val_b,
	         uint64_t intersect_low, uint64_t intersect_high,
	         uint64_t remainder_low, uint64_t remainder_high,
	         itvmap_t<entry_c_t> *map_out);


template <typename entry_t>
bool itvmap_intersect_pred(
	const entry_t *val, const entry_t *other,
	uint64_t low, uint64_t high, const itvmap_t<entry_t> *map
) {
	return low < high;
}


template <typename entry_t>
bool itvmap_dummy_true_proc(
	entry_t *val, entry_t *other,
	uint64_t low, uint64_t high,
	itvmap_t<entry_t> *map, itvmap_it_t<entry_t> *it,
	itvmap_t<entry_t> *insert_map, std::list<itvmap_it_t<entry_t>> *remove_map
) {
	return true;
}


template <typename entry_t>
INLINE
bool itvmap_insert(entry_t val, itvmap_t<entry_t> *map) {
#if 0
	size_t num_elements = 0;

	auto range = map->equal_range(val.itv);

	for (auto it = range.first; it != range.second; ++it) {
		if (it->second == val) {
			num_elements += 1;
		}
	}

	if (num_elements > 0) {
		// printf("%p Inserting val %s %p %lu (%lu existing)\n",
		// 	map, itv_mode_str(val.itv.mode), val.itv.lowptr, val.itv.high - val.itv.low, num_elements);
		// I'm not 100% sure, but we can end up having duplicates by
		// simply adding fragments in reverse orders: e.g.,
		// insert left(a), right(b); THEN insert right(a), left(b)
		// If a == b, we have a duplicate.
		return false;
	}
#endif

	map->insert(itvmap_pair_t<entry_t>(val.itv, val));
	return true;
}


template <typename entry_t>
bool itvmap_fragment_left(entry_t *value, uint64_t middle, itvmap_t<entry_t> *map) {
	entry_t newvalue;

	if (value->itv.low < middle) {
		newvalue = *value;
		newvalue.itv.high = middle;

		return itvmap_insert(newvalue, map);
	}

	return true;
}


template <typename entry_t>
bool itvmap_fragment_right(entry_t *value, uint64_t middle, itvmap_t<entry_t> *map) {
	entry_t newvalue;

	if (value->itv.high > middle) {
		newvalue = *value;
		newvalue.itv.low = middle;

		return itvmap_insert(newvalue, map);
	}

	return true;
}


template <typename entry_t>
INLINE
bool itvmap_fragment_twice(entry_t *value, uint64_t low, uint64_t high, itvmap_t<entry_t> *map) {
	if (!itvmap_fragment_left(value, low, map)) {
		return false;
	}

	if (!itvmap_fragment_right(value, high, map)) {
		return false;
	}

	return true;
}


template <typename entry_t>
INLINE
size_t itvmap_remove(entry_t val, itvmap_t<entry_t> *map) {
	size_t num_removed = 0;

	auto range = map->equal_range(val.itv);

	for (auto it = range.first; it != range.second; /*++it*/) {
		// It's VERY important to advance the iterator before any
		// processing occurs, because otherwise we may end up
		// trying to advance an iterator that doesn't reference a
		// valid map location anymore
		auto it_copy = it;
		++it_copy;

		if (it->second == val) {
			map->erase(it);
			num_removed += 1;
		}

		it = it_copy;
	}

	// if (num_removed > 1) {
	// 	// printf("Too many elements (%lu) to remove for %p Removing val %s %p %lu.\n",
	// 	// 	num_removed, map, itv_mode_str(val.itv.mode), val.itv.lowptr, val.itv.high - val.itv.low);
	// 	// assert(num_removed == 0);
	// } else {
	// 	// printf("Removed %lu elements for %p Removing val %s %p %lu.\n",
	// 	// 	num_removed, map, itv_mode_str(val.itv.mode), val.itv.lowptr, val.itv.high - val.itv.low);
	// }

	expect(num_removed <= 1,
		"Too many elements to remove.");

	return num_removed;
}


template <typename entry_t>
size_t itvmap_process_touching(
	entry_t *val, itvmap_t<entry_t> *map,
	itvmap_process_pred_func_t<entry_t> pred_func,
	itvmap_process_proc_func_t<entry_t> proc_func
) {
	size_t num;
	entry_t *other;

	itv_t *itva, *itvb;
	uint64_t low, high;

	if (!pred_func) {
		return 0;
	}

	num = 0;
	itva = &val->itv;

	itvmap_t<entry_t> insert_map;
	std::list<itvmap_it_t<entry_t>> remove_map;

	for (auto it = map->begin(); it != map->end(); ++it) {
		other = &it->second;
		itvb = &other->itv;

		// It's VERY important to advance the iterator before any
		// processing occurs, because otherwise we may end up
		// trying to advance an iterator that doesn't reference a
		// valid map location anymore
		// ++it;

		if (itva->low > itvb->high) {
			continue;
		}

		if (itva->high < itvb->low) {
			break;
		}

		low  = std::max(itva->low , itvb->low );
		high = std::min(itva->high, itvb->high);

		if (!pred_func(val, other, low, high, map)) {
			continue;
		}

		if (proc_func && proc_func(val, other, low, high, map, &it, &insert_map, &remove_map)) {
			++num;
		}
	}

	for (auto it = remove_map.begin(); it != remove_map.end(); ++it) {
		// auto it_copy = it;
		// ++it_copy;
		itvmap_it_t<entry_t> map_it = (*it);

		map->erase(map_it);

		// it = it_copy;
	}

	// for (auto &it : remove_map) {
	// 	if (itvmap_remove(it.second, map) == 0) {
	// 		error("Unable to remove element <%p, %p> %s from the map %p.",
	// 			it.first.lowptr, it.first.highptr, itv_mode_str(it.first.mode), map);
	// 	}
	// }

	map->insert(insert_map.begin(), insert_map.end());

	// for (auto it = insert_map.begin(); it != insert_map.end(); ++it) {
	// 	// itvmap_insert(it.second, map);
	// }

	return num;
}


// template <typename entry_t>
// bool itvmap_intersect(entry_t val, itvmap_t<entry_t> *map) {
// 	size_t num_found;

// 	num_found = itvmap_process_touching(&val, map, itvmap_intersect_pred, itvmap_dummy_true_proc);

// 	return (num_found > 0);
// }


template <typename entry_t>
bool itvmap_aggregate_proc(
	entry_t *val, entry_t *other,
	uint64_t low, uint64_t high,
	itvmap_t<entry_t> *map, itvmap_it_t<entry_t> *it,
	itvmap_t<entry_t> *insert_map, std::list<itvmap_it_t<entry_t>> *remove_map
) {
	// if (other->itv == val->itv) {
	// 	return true;
	// }

	if (itv_contains_itv(&other->itv, &val->itv)) {
		return true;
	}

	entry_t newval = *val;

	newval.itv.low  = std::min(other->itv.low , val->itv.low );
	newval.itv.high = std::max(other->itv.high, val->itv.high);

	// size_t num_removed = itvmap_remove(*other, map);

	// if (num_removed == 0) {
	// 	error("Unable to remove other.");
	// }

	// return itvmap_insert(newval, map);

	// itvmap_insert(*other, remove_map);
	remove_map->push_front(*it);
	itvmap_insert(newval, insert_map);

	return true;
}


template <typename entry_t>
bool itvmap_insert_aggregated(entry_t val, itvmap_t<entry_t> *map,
                              itvmap_process_pred_func_t<entry_t> pred_func) {
	size_t num_merged;

	num_merged = itvmap_process_touching(&val, map,
		pred_func,
		itvmap_aggregate_proc
	);

	if (num_merged == 0) {
		return itvmap_insert(val, map);
	}

	return true;
}


template <typename entry_t>
bool itvmap_fragment_proc(
	entry_t *val, entry_t *other,
	uint64_t low, uint64_t high,
	itvmap_t<entry_t> *map, itvmap_it_t<entry_t> *it,
	itvmap_t<entry_t> *insert_map, std::list<itvmap_it_t<entry_t>> *remove_map
) {
	// Important! When we remove, we cannot reference 'other' anymore
	entry_t copy = *other;

	// size_t num_removed = itvmap_remove(*other, map);

	// // The old value is removed
	// if (num_removed == 0) {
	// 	error("Unable to remove other.");
	// }

	// // We add the left- and righ-hand excesses of the
	// // existing (now fragmented) value
	// if (!itvmap_fragment_twice(&copy, low, high, map)) {
	// 	return false;
	// }

	// if (low < high) {
	// 	// In all cases, the new value is added
	// 	copy = *val;
	// 	copy.itv.low  = low;
	// 	copy.itv.high = high;

	// 	return itvmap_insert(copy, map);
	// }

	// itvmap_insert(*other, remove_map);
	remove_map->push_front(*it);

	if (!itvmap_fragment_twice(&copy, low, high, insert_map)) {
		return false;
	}

	if (low < high) {
		// In all cases, the new value is added
		copy = *val;
		copy.itv.low  = low;
		copy.itv.high = high;

		itvmap_insert(copy, insert_map);
	}

	return true;
}


template <typename entry_t>
bool itvmap_insert_fragmented(entry_t val, itvmap_t<entry_t> *map,
                              itvmap_process_pred_func_t<entry_t> pred_func) {
	size_t num_fragmented;

	num_fragmented = itvmap_process_touching(&val, map,
		pred_func,
		itvmap_fragment_proc
	);

	if (num_fragmented == 0) {
		return itvmap_insert(val, map);
	}

	return true;
}


template <typename entry_t>
size_t itvmap_process_all(entry_t *val, itvmap_t<entry_t> *map,
                          itvmap_process_pred_func_t<entry_t> pred_func,
                          itvmap_process_proc_func_t<entry_t> proc_func) {
	size_t num;
	entry_t *other;

	itv_t *itva, *itvb;
	uint64_t low, high;

	if (!pred_func) {
		return 0;
	}

	num = 0;
	itva = &val->itv;

	itvmap_t<entry_t> insert_map;
	std::list<itvmap_it_t<entry_t>> remove_map;

	for (auto it = map->begin(); it != map->end(); ++it) {
		other = &it->second;
		itvb = &other->itv;

		// It's VERY important to advance the iterator before any
		// processing occurs, because otherwise we may end up
		// trying to advance an iterator that doesn't reference a
		// valid map location anymore
		// ++it;

		// if (itva->low > itvb->high) {
		// 	continue;
		// }

		// if (itva->high < itvb->low) {
		// 	break;
		// }

		low  = std::max(itva->low , itvb->low );
		high = std::min(itva->high, itvb->high);

		if (!pred_func(val, other, low, high, map)) {
			continue;
		}

		if (proc_func && proc_func(val, other, low, high, map, &it, &insert_map, &remove_map)) {
			++num;
		}
	}

	for (auto it = remove_map.begin(); it != remove_map.end(); ++it) {
		// auto it_copy = it;
		// ++it_copy;
		itvmap_it_t<entry_t> map_it = (*it);

		map->erase(map_it);

		// it = it_copy;
	}

	// for (auto &it : remove_map) {
	// 	if (itvmap_remove(it.second, map) == 0) {
	// 		error("Unable to remove element <%p, %p> %s from the map %p.",
	// 			it.first.lowptr, it.first.highptr, itv_mode_str(it.first.mode), map);
	// 	}
	// }

	map->insert(insert_map.begin(), insert_map.end());

	return num;
}


template <typename entry_t>
bool itvmap_insert_conditional(entry_t val, itvmap_t<entry_t> *map,
                               itvmap_process_pred_func_t<entry_t> pred_func) {
	size_t num_intersections;

	num_intersections = itvmap_process_all(&val, map,
		pred_func, itvmap_dummy_true_proc);

	if (num_intersections == 0) {
		return itvmap_insert(val, map);
	}

	return false;
}


template <typename entry_t>
bool itvmap_insert_disjoint(entry_t val, itvmap_t<entry_t> *map) {
	size_t num_intersections;

	num_intersections = itvmap_process_touching(&val, map,
		itvmap_intersect_pred,
		itvmap_dummy_true_proc
	);

	if (num_intersections == 0) {
		return itvmap_insert(val, map);
	}

	return false;
}


template <typename entry_t>
bool itvmap_remove_fragmented_proc(
	entry_t *val, entry_t *other,
	uint64_t low, uint64_t high,
	itvmap_t<entry_t> *map, itvmap_it_t<entry_t> *it,
	itvmap_t<entry_t> *insert_map, std::list<itvmap_it_t<entry_t>> *remove_map
) {
	// Important! When we remove, we cannot reference 'other' anymore
	entry_t copy = *other;

	// // The old value is removed
	// if (itvmap_remove(*other, map) == 0) {
	// 	return false;
	// }

	// if (itv_contains_itv(&val->itv, &other->itv)) {
	// 	return true;
	// }

	// // We add the left- and righ-hand excesses of the
	// // existing (now fragmented) value
	// if (!itvmap_fragment_twice(&copy, low, high, map)) {
	// 	return false;
	// }

	// The old value is removed
	// itvmap_insert(*other, remove_map);
	// itvmap_insert(it, remove_map);
	remove_map->push_front(*it);

	if (itv_contains_itv(&val->itv, &other->itv)) {
		return true;
	}

	// We add the left- and righ-hand excesses of the
	// existing (now fragmented) value
	if (!itvmap_fragment_twice(&copy, low, high, insert_map)) {
		return false;
	}

	return true;
}


template <typename entry_t>
bool itvmap_remove_fragmented(entry_t val, itvmap_t<entry_t> *map,
                              itvmap_process_pred_func_t<entry_t> pred_func) {
	size_t num_fragmented;

	num_fragmented = itvmap_process_touching(&val, map,
		pred_func,
		itvmap_remove_fragmented_proc
	);

	if (num_fragmented == 0) {
		return false;
	}

	return true;
}


// template <typename entry_t>
// bool itvmap_remove_conditional_proc(entry_t *val, entry_t *other,
//                                     uint64_t low, uint64_t high, itvmap_t<entry_t> *map) {
// 	if (itvmap_remove(*other, map) == 0) {
// 		return false;
// 	}

// 	return true;
// }


template <typename entry_t>
bool itvmap_remove_conditional(entry_t val, itvmap_t<entry_t> *map,
                               itvmap_process_pred_func_t<entry_t> pred_func) {
	size_t num;
	entry_t *other;

	itv_t *itva, *itvb;
	uint64_t low, high;

	if (!pred_func) {
		return 0;
	}

	num = 0;
	itva = &val.itv;

	auto range = map->equal_range(*itva);

	for (auto it = range.first; it != range.second; /*++it*/) {
		other = &it->second;
		itvb = &other->itv;

		// It's VERY important to advance the iterator before any
		// processing occurs, because otherwise we may end up
		// trying to advance an iterator that doesn't reference a
		// valid map location anymore
		auto it_copy = it;
		++it_copy;

		low  = std::max(itva->low , itvb->low );
		high = std::min(itva->high, itvb->high);

		if (pred_func(&val, other, low, high, map)) {
			map->erase(it);
			num += 1;
		}

		it = it_copy;
	}

	return num;
}


template <typename entry_a_t, typename entry_b_t, typename entry_c_t>
size_t itvmap_combine_mtc(
	itvmap_t<entry_a_t> *map_a,
	itvmap_t<entry_b_t> *map_b,
	itvmap_combine_pred_func_2_t<entry_a_t, entry_b_t, entry_c_t> pred_func,
	itvmap_combine_proc_func_2_t<entry_a_t, entry_b_t, entry_c_t> proc_func,
	itvmap_t<entry_c_t> *map_out
) {
	size_t num;
	entry_a_t *val_a;
	entry_b_t *val_b;

	uint64_t intersect_low, intersect_high;
	uint64_t remainder_low, remainder_high;

	num = 0;

	for (auto it_a = map_a->begin(); it_a != map_a->end(); ++it_a) {
		val_a = &it_a->second;
		val_b = NULL;

		intersect_low = intersect_high = val_a->itv.high;

		remainder_low  = val_a->itv.low;
		remainder_high = val_a->itv.high;

		for (auto it_b = map_b->begin(); it_b != map_b->end(); ++it_b) {
			val_b = &it_b->second;

			if (val_a->itv.low >= val_b->itv.high) {
				continue;
			}

			if (val_a->itv.high <= val_b->itv.low) {
				break;
			}

			intersect_low  = std::max(val_a->itv.low , val_b->itv.low );
			intersect_high = std::min(val_a->itv.high, val_b->itv.high);

			if (!pred_func(
				val_a, val_b,
				intersect_low, intersect_high,
				remainder_low, remainder_high,
				map_out
			)) {
				continue;
			}

			if (proc_func(
				val_a, val_b,
				intersect_low, intersect_high,
				remainder_low, remainder_high,
				map_out
			)) {
				++num;
			}

			remainder_low = intersect_high;
			intersect_low = intersect_high;
		}

		val_b = NULL;
		intersect_low = remainder_high;

		if (!pred_func(
			val_a, val_b,
			intersect_low, intersect_high,
			remainder_low, remainder_high,
			map_out
		)) {
			continue;
		}

		if (proc_func(
			val_a, val_b,
			intersect_low, intersect_high,
			remainder_low, remainder_high,
			map_out
		)) {
			++num;
		}
	}

	return num;
}


template <typename entry_a_t, typename entry_b_t>
size_t itvmap_combine(itvmap_t<entry_a_t> *map_a,
                      itvmap_t<entry_b_t> *map_b,
                      itvmap_combine_pred_func_t<entry_a_t, entry_b_t> pred_func,
                      itvmap_combine_proc_func_t<entry_a_t, entry_b_t> proc_func,
                      itvmap_t<entry_a_t> *map_out) {
	size_t num;
	entry_a_t *val_a;
	entry_b_t *val_b;

	uint64_t intersect_low, intersect_high;
	uint64_t remainder_low, remainder_high;

	num = 0;

	for (auto it_a = map_a->begin(); it_a != map_a->end(); ++it_a) {
		val_a = &it_a->second;
		val_b = NULL;

		intersect_low = intersect_high = val_a->itv.high;

		remainder_low  = val_a->itv.low;
		remainder_high = val_a->itv.high;

		for (auto it_b = map_b->begin(); it_b != map_b->end(); ++it_b) {
			val_b = &it_b->second;

			if (val_a->itv.low >= val_b->itv.high) {
				continue;
			}

			if (val_a->itv.high <= val_b->itv.low) {
				break;
			}

			intersect_low  = std::max(val_a->itv.low , val_b->itv.low );
			intersect_high = std::min(val_a->itv.high, val_b->itv.high);

			// remainder_low  = val_a->itv.low;
			// remainder_high = intersect_low;

			if (!pred_func(
				val_a, val_b,
				intersect_low, intersect_high,
				remainder_low, remainder_high,
				map_out
			)) {
				continue;
			}

			if (proc_func(
				val_a, val_b,
				intersect_low, intersect_high,
				remainder_low, remainder_high,
				map_out
			)) {
				++num;
			}

			remainder_low = intersect_high;
			intersect_low = intersect_high;
		}

		val_b = NULL;
		intersect_low = remainder_high;

		if (!pred_func(
			val_a, val_b,
			intersect_low, intersect_high,
			remainder_low, remainder_high,
			map_out
		)) {
			continue;
		}

		if (proc_func(
			val_a, val_b,
			intersect_low, intersect_high,
			remainder_low, remainder_high,
			map_out
		)) {
			++num;
		}
	}

	return num;
}


// template <typename entry_a_t, typename entry_b_t>
// bool itvmap_combine_relativediff_cond(const entry_a_t *val_a, const entry_b_t *val_b,
//                                       uint64_t intersect_low, uint64_t intersect_high,
//                                       uint64_t remainder_low, uint64_t remainder_high,
//                                       const itvmap_t<entry_a_t> *map_out) {
// 	return remainder_low < remainder_high;
// }


template <typename entry_a_t, typename entry_b_t>
bool itvmap_combine_relativediff_proc(entry_a_t *val_a, const entry_b_t *val_b,
                                      uint64_t intersect_low, uint64_t intersect_high,
                                      uint64_t remainder_low, uint64_t remainder_high,
                                      itvmap_t<entry_a_t> *map_out) {
	if (remainder_low >= intersect_low) {
		return false;
	}

	entry_a_t copy_a = *val_a;

	copy_a.itv.low  = remainder_low;
	copy_a.itv.high = intersect_low;

	return itvmap_insert(copy_a, map_out);
}


template <typename entry_a_t, typename entry_b_t>
size_t itvmap_combine_relativediff(itvmap_t<entry_a_t> *map_a, itvmap_t<entry_b_t> *map_b,
                                   itvmap_combine_pred_func_t<entry_a_t, entry_b_t> pred_func,
                                   itvmap_t<entry_a_t> *map_out) {
	return itvmap_combine(map_a, map_b, pred_func, itvmap_combine_relativediff_proc, map_out);
}


// template <typename entry_a_t, typename entry_b_t>
// bool itvmap_combine_intersection_cond(const entry_a_t *val_a, const entry_b_t *val_b,
//                                       uint64_t intersect_low, uint64_t intersect_high,
//                                       uint64_t remainder_low, uint64_t remainder_high,
//                                       const itvmap_t<entry_a_t> *map_out) {
// 	return intersect_low < intersect_high;
// }


template <typename entry_a_t, typename entry_b_t>
bool itvmap_combine_intersection_proc(entry_a_t *val_a, const entry_b_t *val_b,
                                      uint64_t intersect_low, uint64_t intersect_high,
                                      uint64_t remainder_low, uint64_t remainder_high,
                                      itvmap_t<entry_a_t> *map_out) {
	if (intersect_low >= intersect_high) {
		return false;
	}

	entry_a_t copy_a = *val_a;

	copy_a.itv.low  = intersect_low;
	copy_a.itv.high = intersect_high;

	return itvmap_insert(copy_a, map_out);
}


template <typename entry_a_t, typename entry_b_t>
bool itvmap_combine_intersection_proc_mtc(
	entry_a_t *val_a, const entry_b_t *val_b,
	uint64_t intersect_low, uint64_t intersect_high,
	uint64_t remainder_low, uint64_t remainder_high,
	itvmap_t<mtc_entry_t<entry_a_t, entry_b_t>> *map_out
) {
	if (intersect_low >= intersect_high) {
		return false;
	}

	mtc_entry_t<entry_a_t, entry_b_t> mtc = itvmap_new_mtc(
		intersect_low, intersect_high, MATCH_TYPE_INVALID, *val_a, *val_b
	);

	return itvmap_insert(mtc, map_out);
}


template <typename entry_a_t, typename entry_b_t>
size_t itvmap_combine_intersection(itvmap_t<entry_a_t> *map_a, itvmap_t<entry_b_t> *map_b,
                                   itvmap_combine_pred_func_t<entry_a_t, entry_b_t> pred_func,
                                   itvmap_t<entry_a_t> *map_out) {
	return itvmap_combine(map_a, map_b, pred_func, itvmap_combine_intersection_proc, map_out);
}


template <typename entry_a_t, typename entry_b_t, typename entry_c_t>
size_t itvmap_combine_intersection_mtc(
	itvmap_t<entry_a_t> *map_a, itvmap_t<entry_b_t> *map_b,
	itvmap_combine_pred_func_2_t<entry_a_t, entry_b_t, entry_c_t> pred_func,
	itvmap_t<mtc_entry_t<entry_a_t, entry_b_t>> *map_out
) {
	return itvmap_combine_mtc(map_a, map_b, pred_func, itvmap_combine_intersection_proc_mtc, map_out);
}


#if 0
template <typename entry_a_t, typename entry_b_t>
void itvmap_combine_relativediff(itvmap_t<entry_a_t> *map_a, itvmap_t<entry_b_t> *map_b,
                               itvmap_t<mtc_entry_t<entry_a_t, entry_b_t>> *mtc_map,
                               unsigned int matches, bool fragment) {
	entry_a_t *itv_a;
	entry_b_t *itv_b;

	uint64_t low, high;

	// TODO: Optimize, using log-N lookup
	auto begin_b = map_b->begin();

	for (auto it_a = map_a->begin(); it_a != map_a->end(); ++it_a) {
		itv_a = &it_a->second;
		low = itv_a->itv.low;
		high = itv_a->itv.high;

		for (auto it_b = begin_b; it_b != map_b->end(); ++it_b) {
			itv_b = &it_b->second;

			if (low >= itv_b->itv.high) {
				// We want to keep track of the first interval in B
				// that might overlap with the current interval in A.
				// This is important when it comes to duplicates in A
				// that must be compared with the same subsequence of
				// intervals in B. Notice that interval in B that are
				// left behind by this iterator won't be visited again
				++begin_b;

				// The interval in A is entirely to the right of that
				// in B, so we must advance the iterator of B until
				// either we find an overlapping or the next interval
				// in B is entirely to the right of that in A
				continue;
			}

			if (high <= itv_b->itv.low) {
				// There is no overlapping and the interval in B is
				// entirely to the right of that in A.

				// We must stop the visit over B and restart from the
				// next element in A, because there might be other
				// intervals in A overlapping with previous ones in B.
				// This step is optimized though, as we can use the
				// second iterator for B as the initial position for
				// the next pass over B.
				break;
			}

			// There is an overlapping between the two intervals

			if (itv_a->itv.mode != itv_b->itv.mode) {
				continue;
			}

			// If the interval in A has a left-hand excess, save it
			if (low < itv_b->itv.low) {
				itvmap_insert(
					itvmap_new_match(low, itv_b->itv.low, ITV_MATCH_NONE, itv_a, itv_b),
					mtc_map);

				if (fragment) {
					itvmap_fragment_inside(itv_a, itv_b->itv.low, itv_b->itv.high, map_a);
				}
			}

			// The right-hand excess, if any, may still overlap with
			// subsequent intervals in B, so let's wait
			low = std::min(itv_b->itv.high, high);

			// If there is no right-hand excess, we don't need to
			// check anymore the existence of such excess for further
			// intervals in B
			if (low == high) {
				break;
			}
		}

		if (low < high) {
			// We can safely store the right-excess of the current
			// interval in A in the new map. This can happen for two
			// reasons: either the last interval in B was entirely
			// to the right of that in A (we come here from 'break')
			// or there is no other interval in B. However, we must
			// check that the right-excess is not empty.
			itvmap_insert(
				itvmap_new_match(low, high, ITV_MATCH_NONE, itv_a, itv_b),
				mtc_map);

			if (fragment) {
				// Note that it won't use 'high' to fragment
				itvmap_fragment_inside(itv_a, low, high, map_a);
			}
		}
	}
}





template <typename entry_a_t, typename entry_b_t>
void itvmap_combine_intersection(itvmap_t<entry_a_t> *map_a, itvmap_t<entry_b_t> *map_b,
                               itvmap_t<mtc_entry_t<entry_a_t, entry_b_t>> *mtc_map,
                               unsigned int matches, bool fragment) {
	entry_a_t *itv_a;
	entry_b_t *itv_b;

	itv_match_t match;

	uint64_t low, high, nextlow;

	bool sect_match, addr_match, full_match;

	for (auto it_a = map_a->begin(); it_a != map_a->end(); ++it_a) {
		itv_a = &it_a->second;
		low = itv_a->itv.high;
		high = itv_a->itv.low;
		match = ITV_MATCH_ERR;

		nextlow = low;

		for (auto it_b = map_b->begin(); it_b != map_b->end(); ++it_b) {
			itv_b = &it_b->second;

			sect_match = (itv_a->itv.mode == ITV_MODE_NONE || itv_b->itv.mode == ITV_MODE_NONE);
			addr_match = (itv_a->itv.mode != itv_b->itv.mode && itv_a->itv.mode != ITV_MODE_NONE
			                                                 && itv_b->itv.mode != ITV_MODE_NONE);
			full_match = (itv_a->itv.mode == itv_b->itv.mode && itv_a->itv.mode != ITV_MODE_NONE);

			// printf("Pre-comparing A=%d<%p,%p> with B=%d<%p,%p>\n",
			// 	itv_a->itv.mode, itv_a->itv.lowptr, itv_a->itv.highptr,
			// 	itv_b->itv.mode, itv_b->itv.lowptr, itv_b->itv.highptr);

			if (itv_a->itv.low >= itv_b->itv.high) {
				// The interval in A is entirely to the right of that
				// in B, so we must advance the iterator of B until
				// either we find an overlapping or the next interval
				// in B is entirely to the right of that in A
				continue;
			}

			if (itv_a->itv.high <= itv_b->itv.low) {
				// There is no overlapping and the interval in B is
				// entirely to the right of that in A.
				break;
			}

			// There is an overlapping between the two intervals
			if ((matches & ITV_MATCH_SECTION) && sect_match) {
				match = ITV_MATCH_SECTION;
			}
			else if ((matches & ITV_MATCH_ADDRESS) && addr_match) {
				match = ITV_MATCH_ADDRESS;
			}
			else if ((matches & ITV_MATCH_FULL) && full_match) {
				match = ITV_MATCH_FULL;
			}
			else {
				continue;
			}

			printf("Post-comparing A=%s<%p,%p> with B=%s<%p,%p>\n",
				itv_mode_str(itv_a->itv.mode), itv_a->itv.lowptr, itv_a->itv.highptr,
				itv_mode_str(itv_b->itv.mode), itv_b->itv.lowptr, itv_b->itv.highptr);

			low  = std::max(itv_a->itv.low , itv_b->itv.low );
			high = std::min(itv_a->itv.high, itv_b->itv.high);

			itvmap_insert(
				itvmap_new_match(low, high, match, itv_a, itv_b),
				mtc_map);

			if (fragment) {

				if (itvmap_remove_all(nextlow, itv_a->itv.mode, map_a)) {
					error("AAA");
				}

				if (!itvmap_fragment_outside(itv_a, low, high, map_a)) {
					// Il problema è qui: dopo la prima frammentazione di itv_a,
					// la funzione fallisce perché si lavora su una copia dei
					// valori
					error("BBB");
				}

				if (itv_a->itv.low < low) {
					nextlow = itv_a->itv.low;
				}
				else if (high < itv_a->itv.high) {
					nextlow = high;
				}
				else {
					break;
				}

				printf("Using nextlow = %p\n", (void *)nextlow);
			}
		}
	}
}


// template <typename entry_a_t, typename entry_b_t>
// using itvmap_intersect_func2_t =
// 	bool (*)(entry_a_t *existing, entry_b_t *inserted, uint64_t low, uint64_t high,
// 		itvmap_t<entry_a_t> *map_a, itvmap_t<entry_b_t> *map_b);


// template <typename entry_a_t, typename entry_b_t>
// bool itvmap_process_intersection(itvmap_t<entry_a_t> *map_a,
//                                  itvmap_t<entry_b_t> *map_b,
//                                  itvmap_intersect_func2_t<entry_a_t, entry_b_t> func) {
// 	entry_a_t *itv_a;
// 	entry_b_t *itv_b;

// 	uint64_t low, high;

// 	for (auto it_a = map_a->begin(); it_a != map_a->end(); ++it_a) {
// 		itv_a = &it_a;

// 		for (auto it_b = map_b->begin(); it_b != map_b->end(); ++it_b) {
// 			itv_b = &it_b;

// 			if (it_a->low >= it_b->high) {
// 				// The interval in A is entirely to the right of that
// 				// in B, so we must advance the iterator of B until
// 				// either we find an overlapping or the next interval
// 				// in B is entirely to the right of that in A
// 				continue;
// 			}

// 			if (it_a->high <= it_b->low) {
// 				// There is no overlapping and the interval in B is
// 				// entirely to the right of that in A.
// 				break;
// 			}

// 			low  = std::max(it_a->low , it_b->low );
// 			high = std::min(it_a->high, it_b->high);

// 			if (low >= high) {
// 				continue;
// 			}

// 			if (!func(itv_a, itv_b, low, high, map_a, map_b)) {
// 				return false;
// 			}
// 		}
// 	}

// 	return true;
// }
#endif
