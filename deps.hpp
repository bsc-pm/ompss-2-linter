#if 0
#define DEP_DIMENSION_PTR_T(array)  uint64_t (array)[][3]

#define DEP_DIMENSION_ARRAY_T(array, ndim)  uint64_t (array)[ndim][3]

#define DEP_DIMENSION_ARRAY(array, ndim, size_name, start_name, end_name)   \
	DEP_DIMENSION_ARRAY_T(array, ndim) = {                                    \
		DEP_DIMENSION_ARRAY_REC ## ndim(ndim, size_name, start_name, end_name)  \
	}
#endif

#define DEP_DIMENSION_ARRAY(ndim, size_name, start_name, end_name)            \
	{ DEP_DIMENSION_ARRAY_REC ## ndim(ndim, size_name, start_name, end_name) }  \

#define DEP_DIMENSION_ARRAY_GET_size   0
#define DEP_DIMENSION_ARRAY_GET_start  1
#define DEP_DIMENSION_ARRAY_GET_end    2

#define DEP_DIMENSION_ARRAY_GET(array, dim, name) \
	(array)[(dim)][ DEP_DIMENSION_ARRAY_GET_ ## name ]

#define DEP_DIMENSION_ARRAY_REC8(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY8(ndim, size_name, start_name, end_name)

#define DEP_DIMENSION_ARRAY8(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY_REC7(7, size_name, start_name, end_name),     \
	{ size_name ## ndim, start_name ## ndim, end_name ## ndim }

#define DEP_DIMENSION_ARRAY_REC7(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY7(ndim, size_name, start_name, end_name)

#define DEP_DIMENSION_ARRAY7(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY_REC6(6, size_name, start_name, end_name),     \
	{ size_name ## ndim, start_name ## ndim, end_name ## ndim }

#define DEP_DIMENSION_ARRAY_REC6(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY6(ndim, size_name, start_name, end_name)

#define DEP_DIMENSION_ARRAY6(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY_REC5(5, size_name, start_name, end_name),     \
	{ size_name ## ndim, start_name ## ndim, end_name ## ndim }

#define DEP_DIMENSION_ARRAY_REC5(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY5(ndim, size_name, start_name, end_name)

#define DEP_DIMENSION_ARRAY5(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY_REC4(4, size_name, start_name, end_name),     \
	{ size_name ## ndim, start_name ## ndim, end_name ## ndim }

#define DEP_DIMENSION_ARRAY_REC4(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY4(ndim, size_name, start_name, end_name)

#define DEP_DIMENSION_ARRAY4(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY_REC3(3, size_name, start_name, end_name),     \
	{ size_name ## ndim, start_name ## ndim, end_name ## ndim }

#define DEP_DIMENSION_ARRAY_REC3(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY3(ndim, size_name, start_name, end_name)

#define DEP_DIMENSION_ARRAY3(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY_REC2(2, size_name, start_name, end_name),     \
	{ size_name ## ndim, start_name ## ndim, end_name ## ndim }

#define DEP_DIMENSION_ARRAY_REC2(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY2(ndim, size_name, start_name, end_name)

#define DEP_DIMENSION_ARRAY2(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY_REC1(1, size_name, start_name, end_name),     \
	{ size_name ## ndim, start_name ## ndim, end_name ## ndim }

#define DEP_DIMENSION_ARRAY_REC1(ndim, size_name, start_name, end_name) \
	DEP_DIMENSION_ARRAY1(ndim, size_name, start_name, end_name)

#define DEP_DIMENSION_ARRAY1(ndim, size_name, start_name, end_name) \
	{ size_name ## ndim, start_name ## ndim, end_name ## ndim }


bool dep_intersect_with_NONE_fragment(const dep_entry_t *val, const dep_entry_t *other,
                                      uint64_t low, uint64_t high,
                                      const itvmap_t<dep_entry_t> *map) {
	// printf("Compare %s <%p + %lu> VS %s <%p + %lu> => "
	// 	"INTERSECT AT (%p + %lu)\n",
	// 	itv_mode_str(val->itv.mode), val->itv.lowptr, val->itv.high - val->itv.low,
	// 	itv_mode_str(other->itv.mode), other->itv.lowptr, other->itv.high - other->itv.low ,
	// 	(void *)low, high - low
	// );
	return low < high && other->itv.mode == ITV_MODE_NONE;
}


bool dep_touch_and_is_equal(const dep_entry_t *val, const dep_entry_t *other,
                            uint64_t low, uint64_t high,
                            const itvmap_t<dep_entry_t> *map) {
	return low <= high
	       && val->itv.mode == other->itv.mode
	       && val->dep == other->dep;
}


/*
	Prints dependency information associated to a task: a list of
	all knowns dependencies, with respective dimension information.
 */
VOID dep_str(dep_t *dep, char *dep_string, size_t dep_string_size,
	thread_data_t *thread, task_data_t *task) {
	string name = string(dep->name);
	// FIXME: Extract dependency name
	// name = name.substr(0, name.find("["));

	char dimmsg[dep_string_size];
	size_t nchar = 0;

	for (unsigned int i = dep->ndim; i > 0; --i) {
		nchar += snprintf(dimmsg + nchar, dep_string_size - nchar, "[%lu:%lu;%lu]",
			dep->adim[i - 1].start, dep->adim[i - 1].end, dep->adim[i - 1].size);
	}

	snprintf(dep_string, dep_string_size - nchar, "%s (  <%p-%p>  '%s %s'  %s  )",
		dep_type_str(dep->type, dep->mode),
		(void *) dep->extent_low,
		(void *) dep->extent_high,
		dep_mode_str(dep->mode),
		name.c_str(),
		dimmsg);

	// debug(2, thread, task,
	// 	"Registered %s dependency (  <%p-%p>  '%s %s'  %s  )",
	// 	dep_type_str(dep->type, dep->mode),
	// 	dep->extent_low,
	// 	dep->extent_high,
	// 	dep_mode_str(dep->mode),
	// 	name.c_str(),
	// 	dimmsg);
}


// VOID dep_print_ranges(thread_data_t *thread, task_data_t *task, dep_t *dep) {
// 	for (auto &i : dep->itvmap) {
// 		const dep_entry_t *value = &i.second;

// 		debug(3, thread, task,
// 			"Registered expected range %s [%lu:%lu] <%p:%p>",
// 			itv_mode_str(value->itv.mode),
// 			value->itv.low - dep->extent_low,
// 			value->itv.high - dep->extent_low,
// 			value->itv.lowptr, value->itv.highptr);
// 	}
// }


INLINE
void dep_insert_fragment(dep_t *dep, itvmap_t<dep_entry_t> *map, itv_mode_t mode,
                         uint64_t low, uint64_t high) {
	dep_entry_t value = itvmap_new_dep(mode, low, high, dep);

	// itvmap_insert_fragmented(
	// 	value, map, dep_intersect_with_NONE_fragment);

	itvmap_remove_fragmented(
		value, map, dep_intersect_with_NONE_fragment
	);

	itvmap_insert_aggregated(
		value, map, dep_touch_and_is_equal
	);
}


INLINE
void dep_update_maps_single(dep_t *dep, itv_mode_t mode, uint64_t low, uint64_t high) {
	dep_insert_fragment(dep, &dep->itvmap, mode, low, high);
	dep_insert_fragment(dep, &dep->itvmap_unmatched, mode, low, high);
	dep_insert_fragment(dep, &dep->itvmap_unreleased, mode, low, high);

	if ((dep->mode & DEP_FAKE) == 0) {
		// dep_insert_fragment(dep, &dep->itvmap_unmatched, mode, low, high);
		// dep_insert_fragment(dep, &dep->itvmap_unreleased, mode, low, high);
		dep_insert_fragment(dep, &dep->itvmap_active, mode, low, high);
	}
}


INLINE
void dep_update_maps(dep_t *dep, uint64_t low, uint64_t high) {
	if (dep->mode & DEP_READ) {
		dep_update_maps_single(dep, ITV_MODE_READ, low, high);
	}

	if (dep->mode & DEP_WRITE) {
		dep_update_maps_single(dep, ITV_MODE_WRITE, low, high);
	}
}


VOID dep_create_fragments(
	dep_t *dep, uint64_t extent_low, uint64_t start, uint64_t end,
	uint64_t dim, uint64_t *volumes, dep_dim_t *adim
) {
	// The smallest dimension is in bytes. Higher dimensions use
	// smaller dimensions as reference.
	unsigned int d = dim - 1;

	if (start == 0 && end == 0) {
		start = adim[d].start;
		end   = adim[d].end;
	}

	if (d == 0) {
		expect(extent_low + adim[d].start <= dep->extent_high,
			"The left-hand boundary of a fragment shouldn't exceed the extent of a dependency.");
		expect(extent_low + adim[d].end <= dep->extent_high,
			"The right-hand boundary of a fragment shouldn't exceed the extent of a dependency.");

		dep_update_maps(dep, extent_low + start, extent_low + end);
	}
	else if (adim[d - 1].start == 0 && adim[d - 1].end == adim[d - 1].size) {
		// We aggregate over smaller dimensions in order to save a
		// potentially huge number of calls
		start = start * adim[d - 1].size;
		end   = end   * adim[d - 1].size;

		dep_create_fragments(dep, extent_low, start, end, d, volumes, adim);
	}
	else {
		for (unsigned long b = start; b < end; ++b) {
			dep_create_fragments(dep, extent_low + b * volumes[d], 0, 0, d, volumes, adim);
		}
	}
}


dep_t *dep_create(dep_type_t type, dep_mode_t mode, const char *name,
                       uint64_t extent_low, uint64_t ranges[][3], uint64_t ndim) {
	dep_t *dep = new dep_t;
	expect(dep,
		"Unable to create a new dependency.");

	dep->type = type;
	dep->mode = mode;
	dep->name = name;

	dep->ndim = ndim;
	dep->adim = static_cast<dep_dim_t *>(
		calloc(dep->ndim, sizeof(dep_dim_t)));

	dep->itvmap.clear();
	dep->itvmap_unmatched.clear();
	dep->itvmap_unreleased.clear();
	dep->itvmap_active.clear();

	// Initialize dependency dimensions
	// Each dimension is a tuple <size, start, end>.
	// The smallest dimension is in bytes. Higher dimensions use
	// smaller dimensions as reference.
	// dep_dim_t adim[ndim];
	// 
	// Cache volumes for each dimension
	// A volume is the product of sizes for all smaller dimensions,
	// starting from the highest dimension.
	// volumes = [ size_N, size_N * size_(N-1), ..., size_N * ... * size_1 ]

	uint64_t volumes[ndim] = { 0 };
	uint64_t volume = 1;
	uint64_t extent = 1;

	for (unsigned int i = 0; i < ndim; ++i) {
		uint64_t size  = DEP_DIMENSION_ARRAY_GET(ranges, i, size);
		uint64_t start = DEP_DIMENSION_ARRAY_GET(ranges, i, start);
		uint64_t end   = DEP_DIMENSION_ARRAY_GET(ranges, i, end);

		/* adim[i].size  = */ dep->adim[i].size  = size;
		/* adim[i].start = */ dep->adim[i].start = std::min(start, end);
		/* adim[i].end   = */ dep->adim[i].end   = std::max(start, end);

		volumes[i] = volume;
		volume *= dep->adim[i].size;
		extent *= std::max(dep->adim[i].size, dep->adim[i].end);
	}

	expect(volume <= extent,
		"An overflow occurred when computing the extent of a dependency.");

	dep->extent_low = extent_low;
	dep->extent_high = extent_low + extent;

	// Insert dependency fragments
	if (dep->type == DEP_NORMAL) {
		itvmap_insert(
			itvmap_new_dep(ITV_MODE_NONE, dep->extent_low, dep->extent_high, dep),
			&dep->itvmap
		);
	}

	dep_create_fragments(dep, dep->extent_low, 0, 0, dep->ndim, volumes, dep->adim);

	return dep;
}


INLINE
void dep_destroy(dep_t *dep) {
	delete dep;
}
