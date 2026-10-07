//===----------------------------------------------------------------------===//
//                         DuckDB
//
// datalake/datalake_catalog.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/mutex.hpp"

#include "datalake_util.hpp"

namespace duckdb {
class DatalakeSchemaEntry;

//! Catalog that exposes an object storage folder as a set of schemas, tables and views
class DatalakeCatalog : public Catalog {
public:
	DatalakeCatalog(AttachedDatabase &db, string attach_path);
	~DatalakeCatalog() override;

public:
	void Initialize(bool load_builtin) override;
	void Initialize(optional_ptr<ClientContext> context, bool load_builtin) override;

	string GetCatalogType() override;
	string GetDefaultSchema() const override;

	optional_ptr<CatalogEntry> CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) override;
	void ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) override;
	optional_ptr<SchemaCatalogEntry> LookupSchema(CatalogTransaction transaction, const EntryLookupInfo &schema_lookup,
	                                              OnEntryNotFound if_not_found) override;

	PhysicalOperator &PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner, LogicalCreateTable &op,
	                                     PhysicalOperator &plan) override;
	PhysicalOperator &PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner, LogicalInsert &op,
	                             optional_ptr<PhysicalOperator> plan) override;
	PhysicalOperator &PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner, LogicalDelete &op,
	                             PhysicalOperator &plan) override;
	PhysicalOperator &PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner, LogicalUpdate &op,
	                             PhysicalOperator &plan) override;
	ErrorData SupportsCreateTable(BoundCreateTableInfo &info) override;

	DatabaseSize GetDatabaseSize(ClientContext &context) override;
	bool InMemory() override;
	string GetDBPath() override;
	CatalogLookupBehavior CatalogTypeLookupRule(CatalogType type) const override;

public:
	//! Path of the folder that backs this catalog
	const string &GetRootPath() const {
		return root_path;
	}
	//! Normalized root of the other attached datalake catalog that owns `path`, empty when there is none
	static string FindSourceRoot(const string &path, const DatalakeCatalog &exclude);
	//! List the folder and update the schemas of this catalog
	void Refresh(ClientContext &context);
	//! All objects of a single schema
	vector<DatalakeObject> GetObjects(const string &schema_name);

private:
	void DropSchema(ClientContext &context, DropInfo &info) override;
	//! Create the schema entry for a schema that does not exist yet
	unique_ptr<DatalakeSchemaEntry> CreateSchemaEntry(const string &schema_name);
	//! Guards the registry of attached datalake catalogs
	static mutex &InstanceMutex();
	//! Every datalake catalog that is currently attached
	static vector<DatalakeCatalog *> &Instances();

private:
	string root_path;
	mutable mutex catalog_lock;
	vector<DatalakeObject> objects;
	case_insensitive_map_t<unique_ptr<DatalakeSchemaEntry>> schemas;
	//! Active query for which the folder was listed last
	idx_t refreshed_query;
};

} // namespace duckdb
