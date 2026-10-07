#include "datalake_catalog.hpp"
#include "datalake_schema_entry.hpp"

#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/constants.hpp"
#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/exception/binder_exception.hpp"
#include "duckdb/common/exception/catalog_exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/parsed_data/create_schema_info.hpp"
#include "duckdb/parser/parsed_data/drop_info.hpp"
#include "duckdb/storage/database_size.hpp"
#include "duckdb/transaction/meta_transaction.hpp"
#include "duckdb/transaction/transaction_context.hpp"

namespace duckdb {

DatalakeCatalog::DatalakeCatalog(AttachedDatabase &db, string attach_path)
    : Catalog(db), root_path(std::move(attach_path)), refreshed_query(NumericLimits<idx_t>::Maximum()) {
	if (root_path.size() > 1 && (root_path.back() == '/' || root_path.back() == '\\')) {
		auto separator_pos = root_path.find_last_of("/\\");
		if (separator_pos != string::npos && separator_pos > 0) {
			root_path = root_path.substr(0, separator_pos);
		}
	}
	lock_guard<mutex> guard(InstanceMutex());
	Instances().push_back(this);
}

DatalakeCatalog::~DatalakeCatalog() {
	lock_guard<mutex> guard(InstanceMutex());
	auto &instances = Instances();
	for (auto entry = instances.begin(); entry != instances.end(); ++entry) {
		if (*entry == this) {
			instances.erase(entry);
			break;
		}
	}
}

mutex &DatalakeCatalog::InstanceMutex() {
	static mutex instance_mutex;
	return instance_mutex;
}

vector<DatalakeCatalog *> &DatalakeCatalog::Instances() {
	static vector<DatalakeCatalog *> instances;
	return instances;
}

string DatalakeCatalog::FindSourceRoot(const string &path, const DatalakeCatalog &exclude) {
	auto normalized_path = DatalakeUtil::NormalizePathSeparators(path);
	string best_match;
	lock_guard<mutex> guard(InstanceMutex());
	for (auto &instance : Instances()) {
		if (instance == &exclude) {
			continue;
		}
		auto candidate = DatalakeUtil::NormalizePathSeparators(instance->GetRootPath());
		if (candidate.empty() || candidate.size() >= normalized_path.size() || candidate.size() <= best_match.size()) {
			continue;
		}
		if (normalized_path.compare(0, candidate.size(), candidate) != 0 || normalized_path[candidate.size()] != '/') {
			continue;
		}
		best_match = candidate;
	}
	return best_match;
}

void DatalakeCatalog::Initialize(bool load_builtin) {
}

static idx_t GetActiveQuery(ClientContext &context) {
	if (!context.transaction.HasActiveTransaction()) {
		return NumericLimits<idx_t>::Maximum();
	}
	return context.transaction.ActiveTransaction().GetActiveQuery();
}

void DatalakeCatalog::Initialize(optional_ptr<ClientContext> context, bool load_builtin) {
	if (!context) {
		Initialize(load_builtin);
		return;
	}
	auto &fs = FileSystem::GetFileSystem(*context);
	if (root_path.find("://") == string::npos && !fs.DirectoryExists(root_path)) {
		throw IOException("Datalake attach path \"%s\" does not exist or is not a directory", root_path);
	}
	Refresh(*context);
	Initialize(load_builtin);
}

string DatalakeCatalog::GetCatalogType() {
	return "datalake";
}

string DatalakeCatalog::GetDefaultSchema() const {
	return DatalakeUtil::DEFAULT_SCHEMA_NAME;
}

void DatalakeCatalog::Refresh(ClientContext &context) {
	auto active_query = GetActiveQuery(context);
	{
		lock_guard<mutex> guard(catalog_lock);
		if (refreshed_query == active_query) {
			return;
		}
	}

	auto &fs = FileSystem::GetFileSystem(context);
	auto relative_paths = DatalakeUtil::ListRelativePaths(context, root_path);
	auto new_objects = DatalakeUtil::MapObjects(fs, root_path, relative_paths);

	lock_guard<mutex> guard(catalog_lock);
	if (refreshed_query != active_query) {
		objects = std::move(new_objects);
		refreshed_query = active_query;
	}
	if (schemas.find(DatalakeUtil::DEFAULT_SCHEMA_NAME) == schemas.end()) {
		schemas[DatalakeUtil::DEFAULT_SCHEMA_NAME] = CreateSchemaEntry(DatalakeUtil::DEFAULT_SCHEMA_NAME);
	}
	for (auto &object : objects) {
		if (schemas.find(object.schema) == schemas.end()) {
			schemas[object.schema] = CreateSchemaEntry(object.schema);
		}
	}
}

unique_ptr<DatalakeSchemaEntry> DatalakeCatalog::CreateSchemaEntry(const string &schema_name) {
	CreateSchemaInfo info;
	info.catalog = GetName();
	info.schema = schema_name;
	return make_uniq<DatalakeSchemaEntry>(*this, info, *this);
}

vector<DatalakeObject> DatalakeCatalog::GetObjects(const string &schema_name) {
	vector<DatalakeObject> result;
	lock_guard<mutex> guard(catalog_lock);
	for (auto &object : objects) {
		if (StringUtil::CIEquals(object.schema, schema_name)) {
			result.push_back(object);
		}
	}
	return result;
}

void DatalakeCatalog::ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) {
	Refresh(context);

	vector<reference<SchemaCatalogEntry>> snapshot;
	{
		lock_guard<mutex> guard(catalog_lock);
		for (auto &entry : schemas) {
			snapshot.push_back(*entry.second);
		}
	}
	for (auto &entry : snapshot) {
		callback(entry.get());
	}
}

optional_ptr<SchemaCatalogEntry> DatalakeCatalog::LookupSchema(CatalogTransaction transaction,
                                                               const EntryLookupInfo &schema_lookup,
                                                               OnEntryNotFound if_not_found) {
	auto schema_name = schema_lookup.GetEntryName();
	if (schema_name.empty() || IsInvalidSchema(schema_name)) {
		schema_name = DatalakeUtil::DEFAULT_SCHEMA_NAME;
	}
	if (transaction.HasContext()) {
		Refresh(transaction.GetContext());
	}

	lock_guard<mutex> guard(catalog_lock);
	auto entry = schemas.find(schema_name);
	if (entry == schemas.end()) {
		if (if_not_found != OnEntryNotFound::RETURN_NULL) {
			throw CatalogException("Schema with name \"%s\" not found in datalake catalog \"%s\"", schema_name,
			                       GetName());
		}
		return nullptr;
	}
	return entry->second.get();
}

optional_ptr<CatalogEntry> DatalakeCatalog::CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) {
	// COPY ... FROM DATABASE creates the schema of every object before the objects themselves are copied,
	// IF NOT EXISTS means the schema is allowed to be there already - a folder does not have to exist yet
	if (info.on_conflict != OnCreateConflict::IGNORE_ON_CONFLICT) {
		throw CatalogException("Cannot create schema \"%s\" in datalake catalog \"%s\" - the catalog is read-only",
		                       info.schema, GetName());
	}
	if (transaction.HasContext()) {
		Refresh(transaction.GetContext());
	}
	lock_guard<mutex> guard(catalog_lock);
	auto entry = schemas.find(info.schema);
	if (entry == schemas.end()) {
		schemas[info.schema] = CreateSchemaEntry(info.schema);
		entry = schemas.find(info.schema);
	}
	return entry->second.get();
}

void DatalakeCatalog::DropSchema(ClientContext &context, DropInfo &info) {
	throw CatalogException("Cannot drop schema \"%s\" in datalake catalog \"%s\" - the catalog is read-only",
	                       info.name, GetName());
}

static PhysicalOperator &ThrowReadOnlyCatalog(const string &catalog_name, const string &action) {
	throw CatalogException("Cannot %s in datalake catalog \"%s\" - the catalog is read-only", action, catalog_name);
}

PhysicalOperator &DatalakeCatalog::PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner,
                                                     LogicalCreateTable &op, PhysicalOperator &plan) {
	return ThrowReadOnlyCatalog(GetName(), "create a table");
}

PhysicalOperator &DatalakeCatalog::PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner,
                                              LogicalInsert &op, optional_ptr<PhysicalOperator> plan) {
	return ThrowReadOnlyCatalog(GetName(), "insert into a table");
}

PhysicalOperator &DatalakeCatalog::PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner,
                                              LogicalDelete &op, PhysicalOperator &plan) {
	return ThrowReadOnlyCatalog(GetName(), "delete from a table");
}

PhysicalOperator &DatalakeCatalog::PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner,
                                              LogicalUpdate &op, PhysicalOperator &plan) {
	return ThrowReadOnlyCatalog(GetName(), "update a table");
}

ErrorData DatalakeCatalog::SupportsCreateTable(BoundCreateTableInfo &info) {
	return ErrorData(CatalogException("Cannot create a table in datalake catalog \"%s\" - the catalog is read-only",
	                                  GetName()));
}

DatabaseSize DatalakeCatalog::GetDatabaseSize(ClientContext &context) {
	return DatabaseSize();
}

bool DatalakeCatalog::InMemory() {
	return false;
}

string DatalakeCatalog::GetDBPath() {
	return root_path;
}

CatalogLookupBehavior DatalakeCatalog::CatalogTypeLookupRule(CatalogType type) const {
	switch (type) {
	case CatalogType::TABLE_ENTRY:
	case CatalogType::VIEW_ENTRY:
	case CatalogType::SCHEMA_ENTRY:
	case CatalogType::INDEX_ENTRY:
	case CatalogType::TYPE_ENTRY:
		return CatalogLookupBehavior::STANDARD;
	default:
		return CatalogLookupBehavior::NEVER_LOOKUP;
	}
}

} // namespace duckdb
