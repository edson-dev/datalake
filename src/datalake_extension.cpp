#include "datalake_extension.hpp"
#include "datalake_storage.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	StorageExtension::Register(config, "datalake", make_shared_ptr<DatalakeStorageExtension>());
}

void DatalakeExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string DatalakeExtension::Name() {
	return "datalake";
}

std::string DatalakeExtension::Version() const {
#ifdef EXT_VERSION_DATALAKE
	return EXT_VERSION_DATALAKE;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(datalake, loader) {
	duckdb::LoadInternal(loader);
}
}
