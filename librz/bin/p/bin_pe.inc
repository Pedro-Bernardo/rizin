// SPDX-FileCopyrightText: 2009-2019 nibble <nibble.ds@gmail.com>
// SPDX-FileCopyrightText: 2009-2019 pancake <pancake@nopcode.org>
// SPDX-FileCopyrightText: 2009-2019 alvarofe <alvaro.felipe91@gmail.com>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_types.h>
#include <rz_util.h>
#include <rz_lib.h>
#include <rz_bin.h>
#include "../i/private.h"
#include "pe/pe.h"

static Sdb *get_sdb(RzBinFile *bf) {
	RzBinObject *o = bf->o;
	struct PE_(rz_bin_pe_obj_t) * bin;
	if (!o || !o->bin_obj) {
		return NULL;
	}
	bin = (struct PE_(rz_bin_pe_obj_t) *)o->bin_obj;
	return bin ? bin->kv : NULL;
}

static bool load_buffer(RzBinFile *bf, RzBinObject *obj, RzBuffer *buf, Sdb *sdb) {
	rz_return_val_if_fail(bf && obj && buf, false);
	struct PE_(rz_bin_pe_obj_t) *res = PE_(rz_bin_pe_new_buf)(buf, bf->rbin->verbose);
	if (res) {
		sdb_ns_set(sdb, "info", res->kv);
		obj->bin_obj = res;
		return true;
	}
	return false;
}

static void destroy(RzBinFile *bf) {
	PE_(rz_bin_pe_free)
	((struct PE_(rz_bin_pe_obj_t) *)bf->o->bin_obj);
}

static ut64 baddr(RzBinFile *bf) {
	return PE_(rz_bin_pe_get_image_base)(bf->o->bin_obj);
}

static RzBinAddr *binsym(RzBinFile *bf, RzBinSpecialSymbol type) {
	struct rz_bin_pe_addr_t *peaddr = NULL;
	RzBinAddr *ret = NULL;
	if (bf && bf->o && bf->o->bin_obj) {
		switch (type) {
		case RZ_BIN_SPECIAL_SYMBOL_MAIN:
			peaddr = PE_(rz_bin_pe_get_main_vaddr)(bf->o->bin_obj);
			break;
		default:
			break;
		}
	}
	if (peaddr && (ret = RZ_NEW0(RzBinAddr))) {
		ret->paddr = peaddr->paddr;
		ret->vaddr = peaddr->vaddr;
	}
	free(peaddr);
	return ret;
}

static void add_tls_callbacks(RzBinFile *bf, RzList *list) {
	PE_DWord paddr, vaddr, haddr;
	int count = 0;
	RzBinAddr *ptr = NULL;
	struct PE_(rz_bin_pe_obj_t) *bin = (struct PE_(rz_bin_pe_obj_t) *)(bf->o->bin_obj);
	char *key;

	do {
		key = sdb_fmt("pe.tls_callback%d_paddr", count);
		paddr = sdb_num_get(bin->kv, key, 0);
		if (!paddr) {
			break;
		}

		key = sdb_fmt("pe.tls_callback%d_vaddr", count);
		vaddr = sdb_num_get(bin->kv, key, 0);
		if (!vaddr) {
			break;
		}

		key = sdb_fmt("pe.tls_callback%d_haddr", count);
		haddr = sdb_num_get(bin->kv, key, 0);
		if (!haddr) {
			break;
		}
		if ((ptr = RZ_NEW0(RzBinAddr))) {
			ptr->paddr = paddr;
			ptr->vaddr = vaddr;
			ptr->hpaddr = haddr;
			ptr->type = RZ_BIN_ENTRY_TYPE_TLS;
			rz_list_append(list, ptr);
		}
		count++;
	} while (vaddr);
}

static RzList *entries(RzBinFile *bf) {
	struct rz_bin_pe_addr_t *entry = NULL;
	RzBinAddr *ptr = NULL;
	RzList *ret;

	if (!(ret = rz_list_newf(free))) {
		return NULL;
	}
	if (!(entry = PE_(rz_bin_pe_get_entrypoint)(bf->o->bin_obj))) {
		return ret;
	}
	if ((ptr = RZ_NEW0(RzBinAddr))) {
		ptr->paddr = entry->paddr;
		ptr->vaddr = entry->vaddr;
		ptr->hpaddr = entry->haddr;
		ptr->type = RZ_BIN_ENTRY_TYPE_PROGRAM;
		rz_list_append(ret, ptr);
	}
	free(entry);
	// get TLS callback addresses
	add_tls_callbacks(bf, ret);

	return ret;
}

static ut32 perm_of_section_perm(ut64 perm) {
	ut32 r = 0;
	if (RZ_BIN_PE_SCN_IS_EXECUTABLE(perm)) {
		r |= RZ_PERM_X;
		r |= RZ_PERM_R; // implicit
	}
	if (RZ_BIN_PE_SCN_IS_WRITABLE(perm)) {
		r |= RZ_PERM_W;
	}
	if (RZ_BIN_PE_SCN_IS_READABLE(perm)) {
		r |= RZ_PERM_R;
	}
	if (RZ_BIN_PE_SCN_IS_SHAREABLE(perm)) {
		r |= RZ_PERM_SHAR;
	}
	return r;
}

static RzList *maps(RzBinFile *bf) {
	struct PE_(rz_bin_pe_obj_t) *bin = (struct PE_(rz_bin_pe_obj_t) *)bf->o->bin_obj;
	struct rz_bin_pe_section_t *sections = NULL;
	if (!bin || !(sections = bin->sections)) {
		return NULL;
	}
	RzList *ret = rz_list_newf((RzListFree)rz_bin_map_free);
	if (!ret) {
		return NULL;
	}
	ut64 ba = baddr(bf);
	RzBinMap *map = RZ_NEW0(RzBinMap);
	if (!map) {
		return ret;
	}
	map->name = strdup("header");
	map->paddr = 0;
	map->psize = RZ_MIN(bin->size, RZ_ROUND(bin->nt_headers->optional_header.SizeOfHeaders, bin->nt_headers->optional_header.FileAlignment));
	map->vaddr = ba;
	map->vsize = RZ_ROUND(map->psize, 4096);
	map->perm = RZ_PERM_R;
	rz_list_append(ret, map);
	PE_(rz_bin_pe_check_sections)
	(bin, &sections);
	for (size_t i = 0; !sections[i].last; i++) {
		RzBinMap *map = RZ_NEW0(RzBinMap);
		if (!map) {
			break;
		}
		map->paddr = sections[i].paddr;
		map->name = strdup((char *)sections[i].name);
		map->psize = sections[i].size;
		if (map->psize > bin->size) {
			if (sections[i].vsize < bin->size) {
				map->psize = sections[i].vsize;
			} else {
				// hack give it page size
				map->psize = 4096;
			}
		}
		map->vsize = sections[i].vsize;
		if (!map->vsize && map->psize) {
			map->vsize = map->psize;
		}
		map->vaddr = sections[i].vaddr + ba;
		map->perm = perm_of_section_perm(sections[i].perm);
		rz_list_append(ret, map);
	}
	return ret;
}

static RzList *sections(RzBinFile *bf) {
	RzList *ret = NULL;
	RzBinSection *ptr = NULL;
	struct rz_bin_pe_section_t *sections = NULL;
	struct PE_(rz_bin_pe_obj_t) *bin = (struct PE_(rz_bin_pe_obj_t) *)bf->o->bin_obj;
	ut64 ba = baddr(bf);
	int i;
	if (!(ret = rz_list_newf((RzListFree)rz_bin_section_free))) {
		return NULL;
	}
	if (!bin || !(sections = bin->sections)) {
		rz_list_free(ret);
		return NULL;
	}
	PE_(rz_bin_pe_check_sections)
	(bin, &sections);
	for (i = 0; !sections[i].last; i++) {
		if (!(ptr = RZ_NEW0(RzBinSection))) {
			break;
		}
		ptr->name = strdup((char *)sections[i].name);
		ptr->size = sections[i].size;
		ptr->vsize = sections[i].vsize;
		ptr->flags = sections[i].flags;
		ptr->paddr = sections[i].paddr;
		ptr->vaddr = sections[i].vaddr + ba;
		ptr->perm = perm_of_section_perm(sections[i].perm);
		if ((ptr->perm & RZ_PERM_RW) && !(ptr->perm & RZ_PERM_X) && ptr->size > 0) {
			if (!strcmp(ptr->name, ".rsrc") ||
				!strcmp(ptr->name, ".data") ||
				!strcmp(ptr->name, ".rdata")) {
				ptr->is_data = true;
			}
		}
		rz_list_append(ret, ptr);
	}
	return ret;
}

static void find_pe_overlay(RzBinFile *bf) {
	ut64 pe_overlay_size;
	ut64 pe_overlay_offset = PE_(bin_pe_get_overlay)(bf->o->bin_obj, &pe_overlay_size);
	if (pe_overlay_offset) {
		sdb_num_set(bf->sdb, "pe_overlay.offset", pe_overlay_offset, 0);
		sdb_num_set(bf->sdb, "pe_overlay.size", pe_overlay_size, 0);
	}
}

static inline bool is_thumb(struct PE_(rz_bin_pe_obj_t) * bin, ut64 address) {
	switch (bin->nt_headers->file_header.Machine) {
	case PE_IMAGE_FILE_MACHINE_ARM:
	case PE_IMAGE_FILE_MACHINE_ARMNT:
		return address & 1;
	default:
		return false;
	}
}

static RzList *symbols(RzBinFile *bf) {
	RzList *ret = NULL;
	RzBinSymbol *ptr = NULL;
	struct rz_bin_pe_export_t *symbols = NULL;
	struct rz_bin_pe_import_t *imports = NULL;
	struct PE_(rz_bin_pe_obj_t) *bin = bf->o->bin_obj;
	int i;

	if (!(ret = rz_list_newf((RzListFree)rz_bin_symbol_free))) {
		return NULL;
	}
	int file_bits = PE_(rz_bin_pe_get_bits(bin));
	if ((symbols = PE_(rz_bin_pe_get_exports)(bf->o->bin_obj))) {
		for (i = 0; !symbols[i].last; i++) {
			if (!(ptr = RZ_NEW0(RzBinSymbol))) {
				break;
			}
			ptr->name = strdup((char *)symbols[i].name);
			ptr->libname = *symbols[i].libname ? strdup((char *)symbols[i].libname) : NULL;
			ptr->forwarder = rz_str_constpool_get(&bf->rbin->constpool, (char *)symbols[i].forwarder);
			// strncpy (ptr->bind, "NONE", RZ_BIN_SIZEOF_STRINGS);
			ptr->bind = RZ_BIN_BIND_GLOBAL_STR;
			ptr->type = RZ_BIN_TYPE_FUNC_STR;
			ptr->size = 0;
			if (is_thumb(bin, symbols[i].vaddr)) {
				ptr->bits = 16;
				ptr->vaddr = symbols[i].vaddr - 1;
				ptr->paddr = symbols[i].paddr - 1;
			} else {
				ptr->bits = file_bits;
				ptr->vaddr = symbols[i].vaddr;
				ptr->paddr = symbols[i].paddr;
			}
			ptr->ordinal = symbols[i].ordinal;
			rz_list_append(ret, ptr);
		}
		free(symbols);
	}

	if ((imports = PE_(rz_bin_pe_get_imports)(bf->o->bin_obj))) {
		for (i = 0; !imports[i].last; i++) {
			if (!(ptr = RZ_NEW0(RzBinSymbol))) {
				break;
			}
			// strncpy (ptr->name, (char*)symbols[i].name, RZ_BIN_SIZEOF_STRINGS);
			ptr->name = strdup((const char *)imports[i].name);
			ptr->libname = strdup((const char *)imports[i].libname);
			ptr->is_imported = true;
			// strncpy (ptr->forwarder, (char*)imports[i].forwarder, RZ_BIN_SIZEOF_STRINGS);
			ptr->bind = "NONE";
			ptr->type = RZ_BIN_TYPE_FUNC_STR;
			ptr->size = 0;
			ptr->vaddr = imports[i].vaddr;
			ptr->paddr = imports[i].paddr;
			ptr->ordinal = imports[i].ordinal;
			rz_list_append(ret, ptr);
		}
		free(imports);
	}
	find_pe_overlay(bf);
	return ret;
}

static void filter_import(ut8 *n) {
	int I;
	for (I = 0; n[I]; I++) {
		if (n[I] < 30 || n[I] >= 0x7f) {
			n[I] = 0;
			break;
		}
	}
}

static RzList *imports(RzBinFile *bf) {
	RzList *relocs = NULL;
	RzBinImport *ptr = NULL;
	RzBinReloc *rel = NULL;
	struct rz_bin_pe_import_t *imports = NULL;
	int i;

	if (!bf || !bf->o || !bf->o->bin_obj) {
		return NULL;
	}
	RzList *ret = rz_list_newf((RzListFree)rz_bin_import_free);
	if (!ret) {
		return NULL;
	}

	// XXX: has_canary is causing problems! thus we need to check and clean here until it is fixed!
	if (((struct PE_(rz_bin_pe_obj_t) *)bf->o->bin_obj)->relocs) {
		rz_list_free(((struct PE_(rz_bin_pe_obj_t) *)bf->o->bin_obj)->relocs);
	}

	if (!(relocs = rz_list_newf(free))) {
		free(ret);
		return NULL;
	}
	((struct PE_(rz_bin_pe_obj_t) *)bf->o->bin_obj)->relocs = relocs;

	if (!(imports = PE_(rz_bin_pe_get_imports)(bf->o->bin_obj))) {
		return ret;
	}
	for (i = 0; !imports[i].last; i++) {
		if (!(ptr = RZ_NEW0(RzBinImport))) {
			break;
		}
		filter_import(imports[i].name);
		ptr->name = strdup((char *)imports[i].name);
		ptr->libname = strdup((char *)imports[i].libname);
		ptr->bind = "NONE";
		ptr->type = "FUNC";
		ptr->ordinal = imports[i].ordinal;
		// NOTE(eddyb) a PE hint is just an optional possible DLL export table
		// index for the import. There is no point in exposing it.
		// ptr->hint = imports[i].hint;
		rz_list_append(ret, ptr);

		if (!(rel = RZ_NEW0(RzBinReloc))) {
			break;
		}
#ifdef RZ_BIN_PE64
		rel->type = RZ_BIN_RELOC_64;
#else
		rel->type = RZ_BIN_RELOC_32;
#endif
		rel->additive = 0;
		rel->import = ptr;
		rel->addend = 0;
		{
			ut8 addr[4];
			rz_buf_read_at(bf->buf, imports[i].paddr, addr, 4);
			ut64 newaddr = (ut64)rz_read_le32(&addr);
			rel->vaddr = newaddr;
		}
		rel->paddr = imports[i].paddr;
		rz_list_append(relocs, rel);
	}
	free(imports);
	return ret;
}

static RzList *relocs(RzBinFile *bf) {
	struct PE_(rz_bin_pe_obj_t) *obj = bf->o->bin_obj;
	if (obj) {
		return obj->relocs;
	}
	return NULL;
}

static RzList *libs(RzBinFile *bf) {
	struct rz_bin_pe_lib_t *libs = NULL;
	RzList *ret = NULL;
	char *ptr = NULL;
	int i;

	if (!(ret = rz_list_new())) {
		return NULL;
	}
	ret->free = free;
	if (!(libs = PE_(rz_bin_pe_get_libs)(bf->o->bin_obj))) {
		return ret;
	}
	for (i = 0; !libs[i].last; i++) {
		ptr = strdup(libs[i].name);
		rz_list_append(ret, ptr);
	}
	free(libs);
	return ret;
}

static RzList *resources(RzBinFile *bf) {
	struct PE_(rz_bin_pe_obj_t) *obj = bf->o->bin_obj;
	if (!obj) {
		return NULL;
	}

	RzList *res = rz_list_newf((RzListFree)rz_bin_resource_free);
	if (!res) {
		return NULL;
	}
	rz_pe_resource *rs;
	RzListIter *it;
	size_t index = 0;
	rz_list_foreach (obj->resources, it, rs) {
		RzBinResource *br = RZ_NEW0(RzBinResource);
		if (!br) {
			goto err;
		}

		br->index = index++;
		br->name = strdup(rs->name);
		if (!br->name) {
			rz_bin_resource_free(br);
			goto err;
		}
		br->time = strdup(rs->timestr);
		if (!br->time) {
			rz_bin_resource_free(br);
			goto err;
		}
		br->vaddr = PE_(rz_bin_pe_get_image_base)(obj) + rs->data->OffsetToData;
		br->size = rs->data->Size;
		br->type = strdup(rs->type);
		if (!br->type) {
			rz_bin_resource_free(br);
			goto err;
		}
		br->language = strdup(rs->language);
		if (!br->language) {
			rz_bin_resource_free(br);
			goto err;
		}

		rz_list_append(res, br);
	}
	return res;

err:
	rz_list_free(res);
	return NULL;
}

static int is_dot_net(RzBinFile *bf) {
	struct rz_bin_pe_lib_t *libs = NULL;
	int i;
	if (!(libs = PE_(rz_bin_pe_get_libs)(bf->o->bin_obj))) {
		return false;
	}
	for (i = 0; !libs[i].last; i++) {
		if (!strcmp(libs[i].name, "mscoree.dll")) {
			free(libs);
			return true;
		}
	}
	free(libs);
	return false;
}

static int is_vb6(RzBinFile *bf) {
	struct rz_bin_pe_lib_t *libs = NULL;
	int i;
	if (!(libs = PE_(rz_bin_pe_get_libs)(bf->o->bin_obj))) {
		return false;
	}
	for (i = 0; !libs[i].last; i++) {
		if (!strcmp(libs[i].name, "msvbvm60.dll")) {
			free(libs);
			return true;
		}
	}
	free(libs);
	return false;
}

static int has_canary(RzBinFile *bf) {
	// XXX: We only need imports here but this causes leaks, we need to wait for the below. This is a horrible solution!
	// TODO: use O(1) when imports sdbized
	RzListIter *iter;
	struct PE_(rz_bin_pe_obj_t) *bin = bf->o->bin_obj;
	if (bin) {
		const RzList *relocs_list = bin->relocs;
		RzBinReloc *rel;
		if (relocs_list) {
			rz_list_foreach (relocs_list, iter, rel) {
				if (!strcmp(rel->import->name, "__security_init_cookie")) {
					return true;
				}
			}
		}
	} else { // rz_bin needs this as it will not initialise bin
		const RzList *imports_list = imports(bf);
		RzBinImport *imp;
		if (imports_list) {
			rz_list_foreach (imports_list, iter, imp) {
				if (!strcmp(imp->name, "__security_init_cookie")) {
					return true;
				}
			}
		}
	}
	return false;
}

static int haschr(const RzBinFile *bf, ut16 dllCharacteristic) {
	const ut8 *buf;
	unsigned int idx;
	ut64 sz;
	if (!bf) {
		return false;
	}
	buf = rz_buf_data(bf->buf, &sz);
	if (!buf) {
		return false;
	}
	idx = (buf[0x3c] | (buf[0x3d] << 8));
	if (idx + 0x5E + 1 >= sz) {
		return false;
	}
	// it's funny here idx+0x5E can be 158 and sz 159 but with
	// the cast it reads two bytes until 160
	return ((*(ut16 *)(buf + idx + 0x5E)) & dllCharacteristic);
}

static RzBinInfo *info(RzBinFile *bf) {
	struct PE_(rz_bin_pe_obj_t) * bin;
	SDebugInfo di = { { 0 } };
	RzBinInfo *ret = RZ_NEW0(RzBinInfo);
	ut32 claimed_checksum, actual_checksum, pe_overlay;

	if (!ret) {
		return NULL;
	}
	bin = bf->o->bin_obj;
	ret->file = strdup(bf->file);
	ret->bclass = PE_(rz_bin_pe_get_class)(bf->o->bin_obj);
	ret->rclass = strdup("pe");
	ret->os = PE_(rz_bin_pe_get_os)(bf->o->bin_obj);
	ret->arch = PE_(rz_bin_pe_get_arch)(bf->o->bin_obj);
	ret->machine = PE_(rz_bin_pe_get_machine)(bf->o->bin_obj);
	ret->subsystem = PE_(rz_bin_pe_get_subsystem)(bf->o->bin_obj);
	ret->default_cc = PE_(rz_bin_pe_get_cc)(bf->o->bin_obj);
	if (is_dot_net(bf)) {
		ret->lang = "cil";
	}
	if (is_vb6(bf)) {
		ret->lang = "vb";
	}
	if (PE_(rz_bin_pe_is_dll)(bf->o->bin_obj)) {
		ret->type = strdup("DLL (Dynamic Link Library)");
	} else {
		ret->type = strdup("EXEC (Executable file)");
	}
	claimed_checksum = PE_(bin_pe_get_claimed_checksum)(bf->o->bin_obj);
	actual_checksum = PE_(bin_pe_get_actual_checksum)(bf->o->bin_obj);
	pe_overlay = sdb_num_get(bf->sdb, "pe_overlay.size", 0);
	ret->bits = PE_(rz_bin_pe_get_bits)(bf->o->bin_obj);
	ret->big_endian = PE_(rz_bin_pe_is_big_endian)(bf->o->bin_obj);
	ret->dbg_info = 0;
	ret->has_canary = has_canary(bf);
	ret->has_nx = haschr(bf, IMAGE_DLL_CHARACTERISTICS_NX_COMPAT);
	ret->has_pi = haschr(bf, IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE);
	ret->claimed_checksum = strdup(sdb_fmt("0x%08x", claimed_checksum));
	ret->actual_checksum = strdup(sdb_fmt("0x%08x", actual_checksum));
	ret->pe_overlay = pe_overlay > 0;
	ret->signature = bin ? bin->is_signed : false;
	Sdb *db = sdb_ns(bf->sdb, "pe", true);
	sdb_bool_set(db, "canary", has_canary(bf), 0);
	sdb_bool_set(db, "highva", haschr(bf, IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA), 0);
	sdb_bool_set(db, "aslr", haschr(bf, IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE), 0);
	sdb_bool_set(db, "forceintegrity", haschr(bf, IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY), 0);
	sdb_bool_set(db, "nx", haschr(bf, IMAGE_DLL_CHARACTERISTICS_NX_COMPAT), 0);
	sdb_bool_set(db, "isolation", !haschr(bf, IMAGE_DLL_CHARACTERISTICS_FORCE_INTEGRITY), 0);
	sdb_bool_set(db, "seh", !haschr(bf, IMAGE_DLLCHARACTERISTICS_NO_SEH), 0);
	sdb_bool_set(db, "bind", !haschr(bf, IMAGE_DLLCHARACTERISTICS_NO_BIND), 0);
	sdb_bool_set(db, "appcontainer", haschr(bf, IMAGE_DLLCHARACTERISTICS_APPCONTAINER), 0);
	sdb_bool_set(db, "wdmdriver", haschr(bf, IMAGE_DLLCHARACTERISTICS_WDM_DRIVER), 0);
	sdb_bool_set(db, "guardcf", haschr(bf, IMAGE_DLLCHARACTERISTICS_GUARD_CF), 0);
	sdb_bool_set(db, "terminalserveraware", haschr(bf, IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE), 0);
	sdb_num_set(db, "bits", ret->bits, 0);
	sdb_set(db, "claimed_checksum", ret->claimed_checksum, 0);
	sdb_set(db, "actual_checksum", ret->actual_checksum, 0);
	sdb_bool_set(db, "is_authhash_valid", PE_(bin_pe_is_authhash_valid)(bf->o->bin_obj), 0);

	ret->has_va = true;

	if (PE_(rz_bin_pe_is_stripped_debug)(bf->o->bin_obj)) {
		ret->dbg_info |= RZ_BIN_DBG_STRIPPED;
	}
	if (PE_(rz_bin_pe_is_stripped_line_nums)(bf->o->bin_obj)) {
		ret->dbg_info |= RZ_BIN_DBG_LINENUMS;
	}
	if (PE_(rz_bin_pe_is_stripped_local_syms)(bf->o->bin_obj)) {
		ret->dbg_info |= RZ_BIN_DBG_SYMS;
	}
	if (PE_(rz_bin_pe_is_stripped_relocs)(bf->o->bin_obj)) {
		ret->dbg_info |= RZ_BIN_DBG_RELOCS;
	}
	if (PE_(rz_bin_pe_get_debug_data)(bf->o->bin_obj, &di)) {
		ret->guid = rz_str_ndup(di.guidstr, GUIDSTR_LEN);
		if (ret->guid) {
			ret->debug_file_name = rz_str_ndup(di.file_name, DBG_FILE_NAME_LEN);
			if (!ret->debug_file_name) {
				RZ_FREE(ret->guid);
			}
		}
	}

	return ret;
}

static ut64 get_vaddr(RzBinFile *bf, ut64 baddr, ut64 paddr, ut64 vaddr) {
	return baddr + vaddr;
}

static RzList *compute_hashes(RzBinFile *bf) {
	RzList *file_hashes = rz_list_newf((RzListFree)rz_bin_file_hash_free);
	const char *authentihash = PE_(bin_pe_compute_authentihash)(bf->o->bin_obj);
	if (authentihash) {
		RzBinFileHash *authhash = RZ_NEW0(RzBinFileHash);
		if (authhash) {
			authhash->type = strdup("authentihash");
			authhash->hex = authentihash;
			rz_list_push(file_hashes, authhash);
		}
	}

	return file_hashes;
}
