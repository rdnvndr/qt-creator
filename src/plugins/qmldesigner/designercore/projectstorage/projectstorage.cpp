// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "projectstorage.h"

#include <sqlitedatabase.h>

namespace QmlDesigner {

struct ProjectStorage::Statements
{
    Statements(Sqlite::Database &database)
        : database{database}
    {}

    Sqlite::Database &database;
    Sqlite::ReadWriteStatement<1, 2> insertTypeStatement{
        "INSERT OR IGNORE INTO types(sourceId, name) VALUES(?1, ?2) RETURNING typeId", database};
    Sqlite::WriteStatement<5> updatePrototypeAndExtensionStatement{
        "UPDATE types SET prototypeId=?2, prototypeNameId=?3, extensionId=?4, extensionNameId=?5 "
        "WHERE typeId=?1 AND (prototypeId IS NOT ?2 OR extensionId IS NOT ?3 AND prototypeId "
        "IS NOT ?4 OR extensionNameId IS NOT ?5)",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectTypeIdByExportedNameStatement{
        "SELECT typeId FROM exportedTypeNames WHERE name=?1", database};
    mutable Sqlite::ReadStatement<1, 2> selectTypeIdByModuleIdAndExportedNameStatement{
        "SELECT typeId FROM exportedTypeNames "
        "WHERE moduleId=?1 AND name=?2 "
        "ORDER BY majorVersion DESC, minorVersion DESC "
        "LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 3> selectTypeIdByModuleIdAndExportedNameAndMajorVersionStatement{
        "SELECT typeId FROM exportedTypeNames "
        "WHERE moduleId=?1 AND name=?2 AND majorVersion=?3"
        "ORDER BY minorVersion DESC "
        "LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 4> selectTypeIdByModuleIdAndExportedNameAndVersionStatement{
        "SELECT typeId FROM exportedTypeNames "
        "WHERE moduleId=?1 AND name=?2 AND majorVersion=?3 AND minorVersion<=?4"
        "ORDER BY minorVersion DESC "
        "LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<3, 1> selectPropertyDeclarationResultByPropertyDeclarationIdStatement{
        "SELECT propertyTypeId, propertyDeclarationId, propertyTraits "
        "FROM propertyDeclarations "
        "WHERE propertyDeclarationId=?1 "
        "LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectSourceContextIdFromSourceContextsBySourceContextPathStatement{
        "SELECT sourceContextId FROM sourceContexts WHERE sourceContextPath = ?", database};
    mutable Sqlite::ReadStatement<1, 1> selectSourceContextPathFromSourceContextsBySourceContextIdStatement{
        "SELECT sourceContextPath FROM sourceContexts WHERE sourceContextId = ?", database};
    mutable Sqlite::ReadStatement<2> selectAllSourceContextsStatement{
        "SELECT sourceContextPath, sourceContextId FROM sourceContexts", database};
    Sqlite::WriteStatement<1> insertIntoSourceContextsStatement{
        "INSERT INTO sourceContexts(sourceContextPath) VALUES (?)", database};
    mutable Sqlite::ReadStatement<1, 2> selectSourceIdFromSourcesBySourceContextIdAndSourceNameStatement{
        "SELECT sourceId FROM sources WHERE sourceContextId = ? AND sourceName = ?", database};
    mutable Sqlite::ReadStatement<2, 1> selectSourceNameAndSourceContextIdFromSourcesBySourceIdStatement{
        "SELECT sourceName, sourceContextId FROM sources WHERE sourceId = ?", database};
    mutable Sqlite::ReadStatement<1, 1> selectSourceContextIdFromSourcesBySourceIdStatement{
        "SELECT sourceContextId FROM sources WHERE sourceId = ?", database};
    Sqlite::WriteStatement<2> insertIntoSourcesStatement{
        "INSERT INTO sources(sourceContextId, sourceName) VALUES (?,?)", database};
    mutable Sqlite::ReadStatement<3> selectAllSourcesStatement{
        "SELECT sourceName, sourceContextId, sourceId  FROM sources", database};
    mutable Sqlite::ReadStatement<8, 1> selectTypeByTypeIdStatement{
        "SELECT sourceId, t.name, t.typeId, prototypeId, extensionId, traits, annotationTraits, "
        "pd.name "
        "FROM types AS t LEFT JOIN propertyDeclarations AS pd ON "
        "defaultPropertyId=propertyDeclarationId "
        "WHERE t.typeId=?",
        database};
    mutable Sqlite::ReadStatement<4, 1> selectExportedTypesByTypeIdStatement{
        "SELECT moduleId, name, ifnull(majorVersion, -1), ifnull(minorVersion, -1) FROM "
        "exportedTypeNames WHERE typeId=?",
        database};
    mutable Sqlite::ReadStatement<4, 2> selectExportedTypesByTypeIdAndSourceIdStatement{
        "SELECT etn.moduleId, name, ifnull(etn.majorVersion, -1), ifnull(etn.minorVersion, -1) "
        "FROM exportedTypeNames AS etn JOIN documentImports USING(moduleId) WHERE typeId=?1 AND "
        "sourceId=?2",
        database};
    mutable Sqlite::ReadStatement<8> selectTypesStatement{
        "SELECT sourceId, t.name, t.typeId, prototypeId, extensionId, traits, annotationTraits, "
        "pd.name "
        "FROM types AS t LEFT JOIN propertyDeclarations AS pd ON "
        "defaultPropertyId=propertyDeclarationId",
        database};
    Sqlite::WriteStatement<2> updateTypeTraitStatement{
        "UPDATE types SET traits = ?2 WHERE typeId=?1", database};
    Sqlite::WriteStatement<2> updateTypeAnnotationTraitStatement{
        "UPDATE types SET annotationTraits = ?2 WHERE typeId=?1", database};
    Sqlite::ReadStatement<1, 2> selectNotUpdatedTypesInSourcesStatement{
        "SELECT DISTINCT typeId FROM types WHERE (sourceId IN carray(?1) AND typeId NOT IN "
        "carray(?2))",
        database};
    Sqlite::WriteStatement<1> deleteTypeNamesByTypeIdStatement{
        "DELETE FROM exportedTypeNames WHERE typeId=?", database};
    Sqlite::WriteStatement<1> deleteEnumerationDeclarationByTypeIdStatement{
        "DELETE FROM enumerationDeclarations WHERE typeId=?", database};
    Sqlite::WriteStatement<1> deletePropertyDeclarationByTypeIdStatement{
        "DELETE FROM propertyDeclarations WHERE typeId=?", database};
    Sqlite::WriteStatement<1> deleteFunctionDeclarationByTypeIdStatement{
        "DELETE FROM functionDeclarations WHERE typeId=?", database};
    Sqlite::WriteStatement<1> deleteSignalDeclarationByTypeIdStatement{
        "DELETE FROM signalDeclarations WHERE typeId=?", database};
    Sqlite::WriteStatement<1> deleteTypeStatement{"DELETE FROM types  WHERE typeId=?", database};
    mutable Sqlite::ReadStatement<4, 1> selectPropertyDeclarationsByTypeIdStatement{
        "SELECT name, propertyTypeId, propertyTraits, (SELECT name FROM "
        "propertyDeclarations WHERE propertyDeclarationId=pd.aliasPropertyDeclarationId) FROM "
        "propertyDeclarations AS pd WHERE typeId=?",
        database};
    Sqlite::ReadStatement<6, 1> selectPropertyDeclarationsForTypeIdStatement{
        "SELECT name, propertyTraits, propertyTypeId, propertyImportedTypeNameId, "
        "propertyDeclarationId, aliasPropertyDeclarationId FROM propertyDeclarations "
        "WHERE typeId=? ORDER BY name",
        database};
    Sqlite::ReadWriteStatement<1, 5> insertPropertyDeclarationStatement{
        "INSERT INTO propertyDeclarations(typeId, name, propertyTypeId, propertyTraits, "
        "propertyImportedTypeNameId, aliasPropertyDeclarationId) VALUES(?1, ?2, ?3, ?4, ?5, NULL) "
        "RETURNING propertyDeclarationId",
        database};
    Sqlite::WriteStatement<4> updatePropertyDeclarationStatement{
        "UPDATE propertyDeclarations SET propertyTypeId=?2, propertyTraits=?3, "
        "propertyImportedTypeNameId=?4, aliasPropertyDeclarationId=NULL WHERE "
        "propertyDeclarationId=?1",
        database};
    Sqlite::WriteStatement<3> updatePropertyAliasDeclarationRecursivelyWithTypeAndTraitsStatement{
        "WITH RECURSIVE "
        "  properties(aliasPropertyDeclarationId) AS ( "
        "    SELECT propertyDeclarationId FROM propertyDeclarations WHERE "
        "      aliasPropertyDeclarationId=?1 "
        "   UNION ALL "
        "     SELECT pd.propertyDeclarationId FROM "
        "       propertyDeclarations AS pd JOIN properties USING(aliasPropertyDeclarationId)) "
        "UPDATE propertyDeclarations AS pd "
        "SET propertyTypeId=?2, propertyTraits=?3 "
        "FROM properties AS p "
        "WHERE pd.propertyDeclarationId=p.aliasPropertyDeclarationId",
        database};
    Sqlite::WriteStatement<1> updatePropertyAliasDeclarationRecursivelyStatement{
        "WITH RECURSIVE "
        "  propertyValues(propertyTypeId, propertyTraits) AS ("
        "    SELECT propertyTypeId, propertyTraits FROM propertyDeclarations "
        "      WHERE propertyDeclarationId=?1), "
        "  properties(aliasPropertyDeclarationId) AS ( "
        "    SELECT propertyDeclarationId FROM propertyDeclarations WHERE "
        "      aliasPropertyDeclarationId=?1 "
        "   UNION ALL "
        "     SELECT pd.propertyDeclarationId FROM "
        "       propertyDeclarations AS pd JOIN properties USING(aliasPropertyDeclarationId)) "
        "UPDATE propertyDeclarations AS pd "
        "SET propertyTypeId=pv.propertyTypeId, propertyTraits=pv.propertyTraits "
        "FROM properties AS p, propertyValues AS pv "
        "WHERE pd.propertyDeclarationId=p.aliasPropertyDeclarationId",
        database};
    Sqlite::WriteStatement<1> deletePropertyDeclarationStatement{
        "DELETE FROM propertyDeclarations WHERE propertyDeclarationId=?", database};
    Sqlite::ReadStatement<3, 1> selectPropertyDeclarationsWithAliasForTypeIdStatement{
        "SELECT name, propertyDeclarationId, aliasPropertyDeclarationId FROM propertyDeclarations "
        "WHERE typeId=? AND aliasPropertyDeclarationId IS NOT NULL ORDER BY name",
        database};
    Sqlite::WriteStatement<5> updatePropertyDeclarationWithAliasAndTypeStatement{
        "UPDATE propertyDeclarations SET propertyTypeId=?2, propertyTraits=?3, "
        "propertyImportedTypeNameId=?4, aliasPropertyDeclarationId=?5 WHERE "
        "propertyDeclarationId=?1",
        database};
    Sqlite::ReadWriteStatement<1, 2> insertAliasPropertyDeclarationStatement{
        "INSERT INTO propertyDeclarations(typeId, name) VALUES(?1, ?2) RETURNING "
        "propertyDeclarationId",
        database};
    mutable Sqlite::ReadStatement<4, 1> selectFunctionDeclarationsForTypeIdStatement{
        "SELECT name, returnTypeName, signature, functionDeclarationId FROM "
        "functionDeclarations WHERE typeId=? ORDER BY name, signature",
        database};
    mutable Sqlite::ReadStatement<3, 1> selectFunctionDeclarationsForTypeIdWithoutSignatureStatement{
        "SELECT name, returnTypeName, functionDeclarationId FROM "
        "functionDeclarations WHERE typeId=? ORDER BY name",
        database};
    mutable Sqlite::ReadStatement<3, 1> selectFunctionParameterDeclarationsStatement{
        "SELECT json_extract(json_each.value, '$.n'), json_extract(json_each.value, '$.tn'), "
        "json_extract(json_each.value, '$.tr') FROM functionDeclarations, "
        "json_each(functionDeclarations.signature) WHERE functionDeclarationId=?",
        database};
    Sqlite::WriteStatement<4> insertFunctionDeclarationStatement{
        "INSERT INTO functionDeclarations(typeId, name, returnTypeName, signature) VALUES(?1, ?2, "
        "?3, ?4)",
        database};
    Sqlite::WriteStatement<3> updateFunctionDeclarationStatement{
        "UPDATE functionDeclarations "
        "SET returnTypeName=?2, signature=?3 "
        "WHERE functionDeclarationId=?1",
        database};
    Sqlite::WriteStatement<1> deleteFunctionDeclarationStatement{
        "DELETE FROM functionDeclarations WHERE functionDeclarationId=?", database};
    mutable Sqlite::ReadStatement<3, 1> selectSignalDeclarationsForTypeIdStatement{
        "SELECT name, signature, signalDeclarationId FROM signalDeclarations WHERE typeId=? ORDER "
        "BY name, signature",
        database};
    mutable Sqlite::ReadStatement<2, 1> selectSignalDeclarationsForTypeIdWithoutSignatureStatement{
        "SELECT name, signalDeclarationId FROM signalDeclarations WHERE typeId=? ORDER BY name",
        database};
    mutable Sqlite::ReadStatement<3, 1> selectSignalParameterDeclarationsStatement{
        "SELECT json_extract(json_each.value, '$.n'), json_extract(json_each.value, '$.tn'), "
        "json_extract(json_each.value, '$.tr') FROM signalDeclarations, "
        "json_each(signalDeclarations.signature) WHERE signalDeclarationId=?",
        database};
    Sqlite::WriteStatement<3> insertSignalDeclarationStatement{
        "INSERT INTO signalDeclarations(typeId, name, signature) VALUES(?1, ?2, ?3)", database};
    Sqlite::WriteStatement<2> updateSignalDeclarationStatement{
        "UPDATE signalDeclarations SET  signature=?2 WHERE signalDeclarationId=?1", database};
    Sqlite::WriteStatement<1> deleteSignalDeclarationStatement{
        "DELETE FROM signalDeclarations WHERE signalDeclarationId=?", database};
    mutable Sqlite::ReadStatement<3, 1> selectEnumerationDeclarationsForTypeIdStatement{
        "SELECT name, enumeratorDeclarations, enumerationDeclarationId FROM "
        "enumerationDeclarations WHERE typeId=? ORDER BY name",
        database};
    mutable Sqlite::ReadStatement<2, 1> selectEnumerationDeclarationsForTypeIdWithoutEnumeratorDeclarationsStatement{
        "SELECT name, enumerationDeclarationId FROM enumerationDeclarations WHERE typeId=? ORDER "
        "BY name",
        database};
    mutable Sqlite::ReadStatement<3, 1> selectEnumeratorDeclarationStatement{
        "SELECT json_each.key, json_each.value, json_each.type!='null' FROM "
        "enumerationDeclarations, json_each(enumerationDeclarations.enumeratorDeclarations) WHERE "
        "enumerationDeclarationId=?",
        database};
    Sqlite::WriteStatement<3> insertEnumerationDeclarationStatement{
        "INSERT INTO enumerationDeclarations(typeId, name, enumeratorDeclarations) VALUES(?1, ?2, "
        "?3)",
        database};
    Sqlite::WriteStatement<2> updateEnumerationDeclarationStatement{
        "UPDATE enumerationDeclarations SET  enumeratorDeclarations=?2 WHERE "
        "enumerationDeclarationId=?1",
        database};
    Sqlite::WriteStatement<1> deleteEnumerationDeclarationStatement{
        "DELETE FROM enumerationDeclarations WHERE enumerationDeclarationId=?", database};
    mutable Sqlite::ReadStatement<1, 1> selectModuleIdByNameStatement{
        "SELECT moduleId FROM modules WHERE name=? LIMIT 1", database};
    mutable Sqlite::ReadWriteStatement<1, 1> insertModuleNameStatement{
        "INSERT INTO modules(name) VALUES(?1) RETURNING moduleId", database};
    mutable Sqlite::ReadStatement<1, 1> selectModuleNameStatement{
        "SELECT name FROM modules WHERE moduleId =?1", database};
    mutable Sqlite::ReadStatement<2> selectAllModulesStatement{"SELECT name, moduleId FROM modules",
                                                               database};
    mutable Sqlite::ReadStatement<1, 2> selectTypeIdBySourceIdAndNameStatement{
        "SELECT typeId FROM types WHERE sourceId=?1 and name=?2", database};
    mutable Sqlite::ReadStatement<1, 3> selectTypeIdByModuleIdsAndExportedNameStatement{
        "SELECT typeId FROM exportedTypeNames WHERE moduleId IN carray(?1, ?2, 'int32') AND "
        "name=?3",
        database};
    mutable Sqlite::ReadStatement<4> selectAllDocumentImportForSourceIdStatement{
        "SELECT moduleId, majorVersion, minorVersion, sourceId "
        "FROM documentImports ",
        database};
    mutable Sqlite::ReadStatement<5, 2> selectDocumentImportForSourceIdStatement{
        "SELECT importId, sourceId, moduleId, majorVersion, minorVersion "
        "FROM documentImports WHERE sourceId IN carray(?1) AND kind=?2 ORDER BY sourceId, "
        "moduleId, majorVersion, minorVersion",
        database};
    Sqlite::ReadWriteStatement<1, 5> insertDocumentImportWithoutVersionStatement{
        "INSERT INTO documentImports(sourceId, moduleId, sourceModuleId, kind, "
        "parentImportId) VALUES (?1, ?2, ?3, ?4, ?5) RETURNING importId",
        database};
    Sqlite::ReadWriteStatement<1, 6> insertDocumentImportWithMajorVersionStatement{
        "INSERT INTO documentImports(sourceId, moduleId, sourceModuleId, kind, majorVersion, "
        "parentImportId) VALUES (?1, ?2, ?3, ?4, ?5, ?6) RETURNING importId",
        database};
    Sqlite::ReadWriteStatement<1, 7> insertDocumentImportWithVersionStatement{
        "INSERT INTO documentImports(sourceId, moduleId, sourceModuleId, kind, majorVersion, "
        "minorVersion, parentImportId) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) RETURNING "
        "importId",
        database};
    Sqlite::WriteStatement<1> deleteDocumentImportStatement{
        "DELETE FROM documentImports WHERE importId=?1", database};
    Sqlite::WriteStatement<2> deleteDocumentImportsWithParentImportIdStatement{
        "DELETE FROM documentImports WHERE sourceId=?1 AND parentImportId=?2", database};
    Sqlite::WriteStatement<1> deleteDocumentImportsWithSourceIdsStatement{
        "DELETE FROM documentImports WHERE sourceId IN carray(?1)", database};
    mutable Sqlite::ReadStatement<1, 2> selectPropertyDeclarationIdByTypeIdAndNameStatement{
        "SELECT propertyDeclarationId "
        "FROM propertyDeclarations "
        "WHERE typeId=?1 AND name=?2 "
        "LIMIT 1",
        database};
    Sqlite::WriteStatement<2> updateAliasIdPropertyDeclarationStatement{
        "UPDATE propertyDeclarations SET aliasPropertyDeclarationId=?2  WHERE "
        "aliasPropertyDeclarationId=?1",
        database};
    Sqlite::WriteStatement<2> updateAliasPropertyDeclarationByAliasPropertyDeclarationIdStatement{
        "UPDATE propertyDeclarations SET propertyTypeId=new.propertyTypeId, "
        "propertyTraits=new.propertyTraits, aliasPropertyDeclarationId=?1 FROM (SELECT "
        "propertyTypeId, propertyTraits FROM propertyDeclarations WHERE propertyDeclarationId=?1) "
        "AS new WHERE aliasPropertyDeclarationId=?2",
        database};
    Sqlite::WriteStatement<1> updateAliasPropertyDeclarationToNullStatement{
        "UPDATE propertyDeclarations SET aliasPropertyDeclarationId=NULL, propertyTypeId=NULL, "
        "propertyTraits=NULL WHERE propertyDeclarationId=? AND (aliasPropertyDeclarationId IS NOT "
        "NULL OR propertyTypeId IS NOT NULL OR propertyTraits IS NOT NULL)",
        database};
    Sqlite::ReadStatement<5, 1> selectAliasPropertiesDeclarationForPropertiesWithTypeIdStatement{
        "SELECT alias.typeId, alias.propertyDeclarationId, alias.propertyImportedTypeNameId, "
        "  alias.aliasPropertyDeclarationId, alias.aliasPropertyDeclarationTailId "
        "FROM propertyDeclarations AS alias JOIN propertyDeclarations AS target "
        "  ON alias.aliasPropertyDeclarationId=target.propertyDeclarationId OR "
        "    alias.aliasPropertyDeclarationTailId=target.propertyDeclarationId "
        "WHERE alias.propertyTypeId=?1 "
        "UNION ALL "
        "SELECT alias.typeId, alias.propertyDeclarationId, alias.propertyImportedTypeNameId, "
        "  alias.aliasPropertyDeclarationId, alias.aliasPropertyDeclarationTailId "
        "FROM propertyDeclarations AS alias JOIN propertyDeclarations AS target "
        "  ON alias.aliasPropertyDeclarationId=target.propertyDeclarationId OR "
        "    alias.aliasPropertyDeclarationTailId=target.propertyDeclarationId "
        "WHERE target.typeId=?1 "
        "UNION ALL "
        "SELECT alias.typeId, alias.propertyDeclarationId, alias.propertyImportedTypeNameId, "
        "  alias.aliasPropertyDeclarationId, alias.aliasPropertyDeclarationTailId "
        "FROM propertyDeclarations AS alias JOIN propertyDeclarations AS target "
        "  ON alias.aliasPropertyDeclarationId=target.propertyDeclarationId OR "
        "    alias.aliasPropertyDeclarationTailId=target.propertyDeclarationId "
        "WHERE  alias.propertyImportedTypeNameId IN "
        "  (SELECT importedTypeNameId FROM exportedTypeNames JOIN importedTypeNames USING(name) "
        "   WHERE typeId=?1)",
        database};
    Sqlite::ReadStatement<3, 1> selectAliasPropertiesDeclarationForPropertiesWithAliasIdStatement{
        "WITH RECURSIVE "
        "  properties(propertyDeclarationId, propertyImportedTypeNameId, typeId, "
        "    aliasPropertyDeclarationId) AS ("
        "      SELECT propertyDeclarationId, propertyImportedTypeNameId, typeId, "
        "        aliasPropertyDeclarationId FROM propertyDeclarations WHERE "
        "        aliasPropertyDeclarationId=?1"
        "    UNION ALL "
        "      SELECT pd.propertyDeclarationId, pd.propertyImportedTypeNameId, pd.typeId, "
        "        pd.aliasPropertyDeclarationId FROM propertyDeclarations AS pd JOIN properties AS "
        "        p ON pd.aliasPropertyDeclarationId=p.propertyDeclarationId)"
        "SELECT propertyDeclarationId, propertyImportedTypeNameId, aliasPropertyDeclarationId "
        "  FROM properties",
        database};
    Sqlite::ReadWriteStatement<3, 1> updatesPropertyDeclarationPropertyTypeToNullStatement{
        "UPDATE propertyDeclarations SET propertyTypeId=NULL WHERE propertyTypeId=?1 AND "
        "aliasPropertyDeclarationId IS NULL RETURNING typeId, propertyDeclarationId, "
        "propertyImportedTypeNameId",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectPropertyNameStatement{
        "SELECT name FROM propertyDeclarations WHERE propertyDeclarationId=?", database};
    Sqlite::WriteStatement<2> updatePropertyDeclarationTypeStatement{
        "UPDATE propertyDeclarations SET propertyTypeId=?2 WHERE propertyDeclarationId=?1", database};
    Sqlite::ReadWriteStatement<2, 1> updatePrototypeIdToNullStatement{
        "UPDATE types SET prototypeId=NULL WHERE prototypeId=?1 RETURNING "
        "typeId, prototypeNameId",
        database};
    Sqlite::ReadWriteStatement<2, 1> updateExtensionIdToNullStatement{
        "UPDATE types SET extensionId=NULL WHERE extensionId=?1 RETURNING "
        "typeId, extensionNameId",
        database};
    Sqlite::WriteStatement<2> updateTypePrototypeStatement{
        "UPDATE types SET prototypeId=?2 WHERE typeId=?1", database};
    Sqlite::WriteStatement<2> updateTypeExtensionStatement{
        "UPDATE types SET extensionId=?2 WHERE typeId=?1", database};
    mutable Sqlite::ReadStatement<1, 1> selectPrototypeAndExtensionIdsStatement{
        "WITH RECURSIVE "
        "  prototypes(typeId) AS (  "
        "      SELECT prototypeId FROM types WHERE typeId=?1 "
        "    UNION ALL "
        "      SELECT extensionId FROM types WHERE typeId=?1 "
        "    UNION ALL "
        "      SELECT prototypeId FROM types JOIN prototypes USING(typeId) "
        "    UNION ALL "
        "      SELECT extensionId FROM types JOIN prototypes USING(typeId)) "
        "SELECT typeId FROM prototypes WHERE typeId IS NOT NULL",
        database};
    Sqlite::WriteStatement<3> updatePropertyDeclarationAliasIdAndTypeNameIdStatement{
        "UPDATE propertyDeclarations SET aliasPropertyDeclarationId=?2, "
        "propertyImportedTypeNameId=?3 WHERE propertyDeclarationId=?1 AND "
        "(aliasPropertyDeclarationId IS NOT ?2 OR propertyImportedTypeNameId IS NOT ?3)",
        database};
    Sqlite::WriteStatement<1> updatetPropertiesDeclarationValuesOfAliasStatement{
        "WITH RECURSIVE "
        "  properties(propertyDeclarationId, propertyTypeId, propertyTraits) AS ( "
        "      SELECT aliasPropertyDeclarationId, propertyTypeId, propertyTraits FROM "
        "       propertyDeclarations WHERE propertyDeclarationId=?1 "
        "   UNION ALL "
        "      SELECT pd.aliasPropertyDeclarationId, pd.propertyTypeId, pd.propertyTraits FROM "
        "        propertyDeclarations AS pd JOIN properties USING(propertyDeclarationId)) "
        "UPDATE propertyDeclarations AS pd SET propertyTypeId=p.propertyTypeId, "
        "  propertyTraits=p.propertyTraits "
        "FROM properties AS p "
        "WHERE pd.propertyDeclarationId=?1 AND p.propertyDeclarationId IS NULL AND "
        "  (pd.propertyTypeId IS NOT p.propertyTypeId OR pd.propertyTraits IS NOT "
        "  p.propertyTraits)",
        database};
    Sqlite::WriteStatement<1> updatePropertyDeclarationAliasIdToNullStatement{
        "UPDATE propertyDeclarations SET aliasPropertyDeclarationId=NULL  WHERE "
        "propertyDeclarationId=?1",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectPropertyDeclarationIdsForAliasChainStatement{
        "WITH RECURSIVE "
        "  properties(propertyDeclarationId) AS ( "
        "    SELECT aliasPropertyDeclarationId FROM propertyDeclarations WHERE "
        "     propertyDeclarationId=?1 "
        "   UNION ALL "
        "     SELECT aliasPropertyDeclarationId FROM propertyDeclarations JOIN properties "
        "       USING(propertyDeclarationId)) "
        "SELECT propertyDeclarationId FROM properties",
        database};
    mutable Sqlite::ReadStatement<3> selectAllFileStatusesStatement{
        "SELECT sourceId, size, lastModified FROM fileStatuses ORDER BY sourceId", database};
    mutable Sqlite::ReadStatement<3, 1> selectFileStatusesForSourceIdsStatement{
        "SELECT sourceId, size, lastModified FROM fileStatuses WHERE sourceId IN carray(?1) ORDER "
        "BY sourceId",
        database};
    mutable Sqlite::ReadStatement<3, 1> selectFileStatusesForSourceIdStatement{
        "SELECT sourceId, size, lastModified FROM fileStatuses WHERE sourceId=?1 ORDER BY sourceId",
        database};
    Sqlite::WriteStatement<3> insertFileStatusStatement{
        "INSERT INTO fileStatuses(sourceId, size, lastModified) VALUES(?1, ?2, ?3)", database};
    Sqlite::WriteStatement<1> deleteFileStatusStatement{
        "DELETE FROM fileStatuses WHERE sourceId=?1", database};
    Sqlite::WriteStatement<3> updateFileStatusStatement{
        "UPDATE fileStatuses SET size=?2, lastModified=?3 WHERE sourceId=?1", database};
    Sqlite::ReadStatement<1, 1> selectTypeIdBySourceIdStatement{
        "SELECT typeId FROM types WHERE sourceId=?", database};
    mutable Sqlite::ReadStatement<1, 3> selectImportedTypeNameIdStatement{
        "SELECT importedTypeNameId FROM importedTypeNames WHERE kind=?1 AND importOrSourceId=?2 "
        "AND name=?3 LIMIT 1",
        database};
    mutable Sqlite::ReadWriteStatement<1, 3> insertImportedTypeNameIdStatement{
        "INSERT INTO importedTypeNames(kind, importOrSourceId, name) VALUES (?1, ?2, ?3) "
        "RETURNING importedTypeNameId",
        database};
    mutable Sqlite::ReadStatement<1, 2> selectImportIdBySourceIdAndModuleIdStatement{
        "SELECT importId FROM documentImports WHERE sourceId=?1 AND moduleId=?2 AND majorVersion "
        "IS NULL AND minorVersion IS NULL LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 3> selectImportIdBySourceIdAndModuleIdAndMajorVersionStatement{
        "SELECT importId FROM documentImports WHERE sourceId=?1 AND moduleId=?2 AND "
        "majorVersion=?3 AND minorVersion IS NULL LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 4> selectImportIdBySourceIdAndModuleIdAndVersionStatement{
        "SELECT importId FROM documentImports WHERE sourceId=?1 AND moduleId=?2 AND "
        "majorVersion=?3 AND minorVersion=?4 LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectKindFromImportedTypeNamesStatement{
        "SELECT kind FROM importedTypeNames WHERE importedTypeNameId=?1", database};
    mutable Sqlite::ReadStatement<1, 1> selectNameFromImportedTypeNamesStatement{
        "SELECT name FROM importedTypeNames WHERE importedTypeNameId=?1", database};
    mutable Sqlite::ReadStatement<1, 1> selectTypeIdForQualifiedImportedTypeNameNamesStatement{
        "SELECT typeId FROM importedTypeNames AS itn JOIN documentImports AS di ON "
        "importOrSourceId=di.importId JOIN documentImports AS di2 ON di.sourceId=di2.sourceId AND "
        "di.moduleId=di2.sourceModuleId "
        "JOIN exportedTypeNames AS etn ON di2.moduleId=etn.moduleId WHERE "
        "itn.kind=2 AND importedTypeNameId=?1 AND itn.name=etn.name AND "
        "(di.majorVersion IS NULL OR (di.majorVersion=etn.majorVersion AND (di.minorVersion IS "
        "NULL OR di.minorVersion>=etn.minorVersion))) ORDER BY etn.majorVersion DESC NULLS FIRST, "
        "etn.minorVersion DESC NULLS FIRST LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectTypeIdForImportedTypeNameNamesStatement{
        "WITH "
        "  importTypeNames(moduleId, name, kind, majorVersion, minorVersion) AS ( "
        "    SELECT moduleId, name, di.kind, majorVersion, minorVersion "
        "    FROM importedTypeNames AS itn JOIN documentImports AS di ON "
        "      importOrSourceId=sourceId "
        "    WHERE "
        "      importedTypeNameId=?1 AND itn.kind=1) "
        "SELECT typeId FROM importTypeNames AS itn "
        "  JOIN exportedTypeNames AS etn USING(moduleId, name) "
        "WHERE (itn.majorVersion IS NULL OR (itn.majorVersion=etn.majorVersion "
        "  AND (itn.minorVersion IS NULL OR itn.minorVersion>=etn.minorVersion))) "
        "ORDER BY itn.kind, etn.majorVersion DESC NULLS FIRST, etn.minorVersion DESC NULLS FIRST "
        "LIMIT 1",
        database};
    Sqlite::WriteStatement<0> deleteAllSourcesStatement{"DELETE FROM sources", database};
    Sqlite::WriteStatement<0> deleteAllSourceContextsStatement{"DELETE FROM sourceContexts", database};
    mutable Sqlite::ReadStatement<6, 1> selectExportedTypesForSourceIdsStatement{
        "SELECT moduleId, name, ifnull(majorVersion, -1), ifnull(minorVersion, -1), typeId, "
        "exportedTypeNameId FROM exportedTypeNames WHERE typeId in carray(?1) ORDER BY moduleId, "
        "name, majorVersion, minorVersion",
        database};
    Sqlite::WriteStatement<5> insertExportedTypeNamesWithVersionStatement{
        "INSERT INTO exportedTypeNames(moduleId, name, majorVersion, minorVersion, typeId) "
        "VALUES(?1, ?2, ?3, ?4, ?5)",
        database};
    Sqlite::WriteStatement<4> insertExportedTypeNamesWithMajorVersionStatement{
        "INSERT INTO exportedTypeNames(moduleId, name, majorVersion, typeId) "
        "VALUES(?1, ?2, ?3, ?4)",
        database};
    Sqlite::WriteStatement<3> insertExportedTypeNamesWithoutVersionStatement{
        "INSERT INTO exportedTypeNames(moduleId, name, typeId) VALUES(?1, ?2, ?3)", database};
    Sqlite::WriteStatement<1> deleteExportedTypeNameStatement{
        "DELETE FROM exportedTypeNames WHERE exportedTypeNameId=?", database};
    Sqlite::WriteStatement<2> updateExportedTypeNameTypeIdStatement{
        "UPDATE exportedTypeNames SET typeId=?2 WHERE exportedTypeNameId=?1", database};
    mutable Sqlite::ReadStatement<4, 1> selectProjectDatasForSourceIdsStatement{
        "SELECT projectSourceId, sourceId, moduleId, fileType FROM projectDatas WHERE "
        "projectSourceId IN carray(?1) ORDER BY projectSourceId, sourceId",
        database};
    Sqlite::WriteStatement<4> insertProjectDataStatement{
        "INSERT INTO projectDatas(projectSourceId, sourceId, "
        "moduleId, fileType) VALUES(?1, ?2, ?3, ?4)",
        database};
    Sqlite::WriteStatement<2> deleteProjectDataStatement{
        "DELETE FROM projectDatas WHERE projectSourceId=?1 AND sourceId=?2", database};
    Sqlite::WriteStatement<4> updateProjectDataStatement{
        "UPDATE projectDatas SET moduleId=?3, fileType=?4 WHERE projectSourceId=?1 AND sourceId=?2",
        database};
    mutable Sqlite::ReadStatement<4, 1> selectProjectDatasForSourceIdStatement{
        "SELECT projectSourceId, sourceId, moduleId, fileType FROM projectDatas WHERE "
        "projectSourceId=?1",
        database};
    mutable Sqlite::ReadStatement<4, 1> selectProjectDataForSourceIdStatement{
        "SELECT projectSourceId, sourceId, moduleId, fileType FROM projectDatas WHERE "
        "sourceId=?1 LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectTypeIdsForSourceIdsStatement{
        "SELECT typeId FROM types WHERE sourceId IN carray(?1)", database};
    mutable Sqlite::ReadStatement<6, 1> selectModuleExportedImportsForSourceIdStatement{
        "SELECT moduleExportedImportId, moduleId, exportedModuleId, ifnull(majorVersion, -1), "
        "ifnull(minorVersion, -1), isAutoVersion FROM moduleExportedImports WHERE moduleId IN "
        "carray(?1) ORDER BY moduleId, exportedModuleId",
        database};
    Sqlite::WriteStatement<3> insertModuleExportedImportWithoutVersionStatement{
        "INSERT INTO moduleExportedImports(moduleId, exportedModuleId, isAutoVersion) "
        "VALUES (?1, ?2, ?3)",
        database};
    Sqlite::WriteStatement<4> insertModuleExportedImportWithMajorVersionStatement{
        "INSERT INTO moduleExportedImports(moduleId, exportedModuleId, isAutoVersion, "
        "majorVersion) VALUES (?1, ?2, ?3, ?4)",
        database};
    Sqlite::WriteStatement<5> insertModuleExportedImportWithVersionStatement{
        "INSERT INTO moduleExportedImports(moduleId, exportedModuleId, isAutoVersion, "
        "majorVersion, minorVersion) VALUES (?1, ?2, ?3, ?4, ?5)",
        database};
    Sqlite::WriteStatement<1> deleteModuleExportedImportStatement{
        "DELETE FROM moduleExportedImports WHERE moduleExportedImportId=?1", database};
    mutable Sqlite::ReadStatement<3, 3> selectModuleExportedImportsForModuleIdStatement{
        "WITH RECURSIVE "
        "  imports(moduleId, majorVersion, minorVersion, moduleExportedImportId) AS ( "
        "      SELECT exportedModuleId, "
        "             iif(isAutoVersion=1, ?2, majorVersion), "
        "             iif(isAutoVersion=1, ?3, minorVersion), "
        "             moduleExportedImportId "
        "        FROM moduleExportedImports WHERE moduleId=?1 "
        "    UNION ALL "
        "      SELECT exportedModuleId, "
        "             iif(mei.isAutoVersion=1, i.majorVersion, mei.majorVersion), "
        "             iif(mei.isAutoVersion=1, i.minorVersion, mei.minorVersion), "
        "             mei.moduleExportedImportId "
        "        FROM moduleExportedImports AS mei JOIN imports AS i USING(moduleId)) "
        "SELECT DISTINCT moduleId, ifnull(majorVersion, -1), ifnull(minorVersion, -1) "
        "FROM imports",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectLocalPropertyDeclarationIdsForTypeStatement{
        "SELECT propertyDeclarationId "
        "FROM propertyDeclarations "
        "WHERE typeId=? "
        "ORDER BY propertyDeclarationId",
        database};
    mutable Sqlite::ReadStatement<1, 2> selectLocalPropertyDeclarationIdForTypeAndPropertyNameStatement{
        "SELECT propertyDeclarationId "
        "FROM propertyDeclarations "
        "WHERE typeId=?1 AND name=?2 LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<4, 1> selectPropertyDeclarationForPropertyDeclarationIdStatement{
        "SELECT typeId, name, propertyTraits, propertyTypeId "
        "FROM propertyDeclarations "
        "WHERE propertyDeclarationId=?1 LIMIT 1",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectSignalDeclarationNamesForTypeStatement{
        "WITH RECURSIVE "
        "  all_prototype_and_extension(typeId, prototypeId) AS ("
        "       SELECT typeId, prototypeId FROM types WHERE prototypeId IS NOT NULL"
        "    UNION ALL "
        "       SELECT typeId, extensionId FROM types WHERE extensionId IS NOT NULL),"
        "  typeChain(typeId) AS ("
        "      VALUES(?1)"
        "    UNION ALL "
        "      SELECT prototypeId FROM all_prototype_and_extension JOIN typeChain "
        "        USING(typeId)) "
        "SELECT name FROM typeChain JOIN signalDeclarations "
        "  USING(typeId) ORDER BY name",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectFuncionDeclarationNamesForTypeStatement{
        "WITH RECURSIVE "
        "  all_prototype_and_extension(typeId, prototypeId) AS ("
        "       SELECT typeId, prototypeId FROM types WHERE prototypeId IS NOT NULL"
        "    UNION ALL "
        "       SELECT typeId, extensionId FROM types WHERE extensionId IS NOT NULL),"
        "  typeChain(typeId) AS ("
        "      VALUES(?1)"
        "    UNION ALL "
        "      SELECT prototypeId FROM all_prototype_and_extension JOIN typeChain "
        "        USING(typeId))"
        "SELECT name FROM typeChain JOIN functionDeclarations "
        "  USING(typeId) ORDER BY name",
        database};
    mutable Sqlite::ReadStatement<2> selectTypesWithDefaultPropertyStatement{
        "SELECT typeId, defaultPropertyId FROM types ORDER BY typeId", database};
    Sqlite::WriteStatement<2> updateDefaultPropertyIdStatement{
        "UPDATE types SET defaultPropertyId=?2 WHERE typeId=?1", database};
    Sqlite::WriteStatement<1> updateDefaultPropertyIdToNullStatement{
        "UPDATE types SET defaultPropertyId=NULL WHERE defaultPropertyId=?1", database};
    mutable Sqlite::ReadStatement<3, 1> selectInfoTypeByTypeIdStatement{
        "SELECT sourceId, traits, annotationTraits FROM types WHERE typeId=?", database};
    mutable Sqlite::ReadStatement<1, 1> selectDefaultPropertyDeclarationIdStatement{
        "SELECT defaultPropertyId FROM types WHERE typeId=?", database};
    mutable Sqlite::ReadStatement<1, 1> selectPrototypeIdsForTypeIdInOrderStatement{
        "WITH RECURSIVE "
        "  all_prototype_and_extension(typeId, prototypeId) AS ("
        "       SELECT typeId, prototypeId FROM types WHERE prototypeId IS NOT NULL"
        "    UNION ALL "
        "       SELECT typeId, extensionId FROM types WHERE extensionId IS NOT NULL),"
        "  prototypes(typeId, level) AS ("
        "       SELECT prototypeId, 0 FROM all_prototype_and_extension WHERE typeId=?"
        "    UNION ALL "
        "      SELECT prototypeId, p.level+1 FROM all_prototype_and_extension JOIN "
        "        prototypes AS p USING(typeId)) "
        "SELECT typeId FROM prototypes ORDER BY level",
        database};
    Sqlite::WriteStatement<2> upsertPropertyEditorPathIdStatement{
        "INSERT INTO propertyEditorPaths(typeId, pathSourceId) VALUES(?1, ?2) ON CONFLICT DO "
        "UPDATE SET pathSourceId=excluded.pathSourceId WHERE pathSourceId IS NOT "
        "excluded.pathSourceId",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectPropertyEditorPathIdStatement{
        "SELECT pathSourceId FROM propertyEditorPaths WHERE typeId=?", database};
    mutable Sqlite::ReadStatement<3, 1> selectPropertyEditorPathsForForSourceIdsStatement{
        "SELECT typeId, pathSourceId, directoryId "
        "FROM propertyEditorPaths "
        "WHERE directoryId IN carray(?1) "
        "ORDER BY typeId",
        database};
    Sqlite::WriteStatement<3> insertPropertyEditorPathStatement{
        "INSERT INTO propertyEditorPaths(typeId, pathSourceId, directoryId) VALUES (?1, ?2, ?3)",
        database};
    Sqlite::WriteStatement<3> updatePropertyEditorPathsStatement{
        "UPDATE propertyEditorPaths "
        "SET pathSourceId=?2, directoryId=?3 "
        "WHERE typeId=?1",
        database};
    Sqlite::WriteStatement<1> deletePropertyEditorPathStatement{
        "DELETE FROM propertyEditorPaths WHERE typeId=?1", database};
    mutable Sqlite::ReadStatement<4, 1> selectTypeAnnotationsForSourceIdsStatement{
        "SELECT typeId, iconPath, itemLibrary, hints FROM typeAnnotations WHERE "
        "sourceId IN carray(?1) ORDER BY typeId",
        database};
    Sqlite::WriteStatement<6> insertTypeAnnotationStatement{
        "INSERT INTO "
        "  typeAnnotations(typeId, sourceId, directorySourceId, iconPath, itemLibrary, hints) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6)",
        database};
    Sqlite::WriteStatement<4> updateTypeAnnotationStatement{
        "UPDATE typeAnnotations SET iconPath=?2, itemLibrary=?3, hints=?4 WHERE typeId=?1", database};
    Sqlite::WriteStatement<1> deleteTypeAnnotationStatement{
        "DELETE FROM typeAnnotations WHERE typeId=?1", database};
    mutable Sqlite::ReadStatement<1, 1> selectTypeIconPathStatement{
        "SELECT iconPath FROM typeAnnotations WHERE typeId=?1", database};
    mutable Sqlite::ReadStatement<2, 1> selectTypeHintsStatement{
        "SELECT hints.key, hints.value "
        "FROM typeAnnotations, json_each(typeAnnotations.hints) AS hints "
        "WHERE typeId=?1 AND hints IS NOT NULL",
        database};
    mutable Sqlite::ReadStatement<1, 1> selectTypeAnnotationSourceIdsStatement{
        "SELECT sourceId FROM typeAnnotations WHERE directorySourceId=?1 ORDER BY sourceId", database};
    mutable Sqlite::ReadStatement<1, 0> selectTypeAnnotationDirectorySourceIdsStatement{
        "SELECT DISTINCT directorySourceId FROM typeAnnotations ORDER BY directorySourceId", database};
    mutable Sqlite::ReadStatement<9> selectItemLibraryEntriesStatement{
        "SELECT typeId, i.value->>'$.name', i.value->>'$.iconPath', i.value->>'$.category', "
        "  i.value->>'$.import', i.value->>'$.toolTip', i.value->>'$.properties', "
        "  i.value->>'$.extraFilePaths', i.value->>'$.templatePath' "
        "FROM typeAnnotations AS ta , json_each(ta.itemLibrary) AS i "
        "WHERE ta.itemLibrary IS NOT NULL",
        database};
    mutable Sqlite::ReadStatement<9, 1> selectItemLibraryEntriesByTypeIdStatement{
        "SELECT typeId, i.value->>'$.name', i.value->>'$.iconPath', i.value->>'$.category', "
        "  i.value->>'$.import', i.value->>'$.toolTip', i.value->>'$.properties', "
        "  i.value->>'$.extraFilePaths', i.value->>'$.templatePath' "
        "FROM typeAnnotations AS ta, json_each(ta.itemLibrary) AS i "
        "WHERE typeId=?1 AND ta.itemLibrary IS NOT NULL",
        database};
    mutable Sqlite::ReadStatement<9, 1> selectItemLibraryEntriesBySourceIdStatement{
        "SELECT typeId, i.value->>'$.name', i.value->>'$.iconPath', "
        "i.value->>'$.category', "
        "  i.value->>'$.import', i.value->>'$.toolTip', i.value->>'$.properties', "
        "  i.value->>'$.extraFilePaths', i.value->>'$.templatePath' "
        "FROM typeAnnotations, json_each(typeAnnotations.itemLibrary) AS i "
        "WHERE typeId IN (SELECT DISTINCT typeId "
        "                 FROM documentImports AS di JOIN exportedTypeNames "
        "                   USING(moduleId) "
        "                 WHERE di.sourceId=?)",
        database};
    mutable Sqlite::ReadStatement<3, 1> selectItemLibraryPropertiesStatement{
        "SELECT p.value->>0, p.value->>1, p.value->>2 FROM json_each(?1) AS p", database};
    mutable Sqlite::ReadStatement<1, 1> selectItemLibraryExtraFilePathsStatement{
        "SELECT p.value FROM json_each(?1) AS p", database};
    mutable Sqlite::ReadStatement<1, 1> selectTypeIdsByModuleIdStatement{
        "SELECT DISTINCT typeId FROM exportedTypeNames WHERE moduleId=?", database};
    mutable Sqlite::ReadStatement<1, 1> selectHeirTypeIdsStatement{
        "WITH RECURSIVE "
        "  typeSelection(typeId) AS ("
        "      SELECT typeId FROM types WHERE prototypeId=?1 OR extensionId=?1"
        "    UNION ALL "
        "      SELECT t.typeId "
        "      FROM types AS t JOIN typeSelection AS ts "
        "      WHERE prototypeId=ts.typeId OR extensionId=ts.typeId)"
        "SELECT typeId FROM typeSelection",
        database};
};

class ProjectStorage::Initializer
{
public:
    Initializer(Database &database, bool isInitialized)
    {
        if (!isInitialized) {
            auto moduleIdColumn = createModulesTable(database);
            createSourceContextsTable(database);
            createSourcesTable(database);
            createTypesAndePropertyDeclarationsTables(database, moduleIdColumn);
            createExportedTypeNamesTable(database, moduleIdColumn);
            createImportedTypeNamesTable(database);
            createEnumerationsTable(database);
            createFunctionsTable(database);
            createSignalsTable(database);
            createModuleExportedImportsTable(database, moduleIdColumn);
            createDocumentImportsTable(database, moduleIdColumn);
            createFileStatusesTable(database);
            createProjectDatasTable(database);
            createPropertyEditorPathsTable(database);
            createTypeAnnotionsTable(database);
        }
        database.setIsInitialized(true);
    }

    void createSourceContextsTable(Database &database)
    {
        Sqlite::Table table;
        table.setUseIfNotExists(true);
        table.setName("sourceContexts");
        table.addColumn("sourceContextId", Sqlite::ColumnType::Integer, {Sqlite::PrimaryKey{}});
        const Sqlite::Column &sourceContextPathColumn = table.addColumn("sourceContextPath");

        table.addUniqueIndex({sourceContextPathColumn});

        table.initialize(database);
    }

    void createSourcesTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("sources");
        table.addColumn("sourceId", Sqlite::StrictColumnType::Integer, {Sqlite::PrimaryKey{}});
        const auto &sourceContextIdColumn = table.addColumn(
            "sourceContextId",
            Sqlite::StrictColumnType::Integer,
            {Sqlite::NotNull{},
             Sqlite::ForeignKey{"sourceContexts",
                                "sourceContextId",
                                Sqlite::ForeignKeyAction::NoAction,
                                Sqlite::ForeignKeyAction::Cascade}});
        const auto &sourceNameColumn = table.addColumn("sourceName", Sqlite::StrictColumnType::Text);
        table.addUniqueIndex({sourceContextIdColumn, sourceNameColumn});

        table.initialize(database);
    }

    void createTypesAndePropertyDeclarationsTables(
        Database &database, [[maybe_unused]] const Sqlite::StrictColumn &foreignModuleIdColumn)
    {
        Sqlite::StrictTable typesTable;
        typesTable.setUseIfNotExists(true);
        typesTable.setName("types");
        typesTable.addColumn("typeId", Sqlite::StrictColumnType::Integer, {Sqlite::PrimaryKey{}});
        auto &sourceIdColumn = typesTable.addColumn("sourceId", Sqlite::StrictColumnType::Integer);
        auto &typesNameColumn = typesTable.addColumn("name", Sqlite::StrictColumnType::Text);
        typesTable.addColumn("traits", Sqlite::StrictColumnType::Integer);
        auto &prototypeIdColumn = typesTable.addForeignKeyColumn("prototypeId",
                                                                 typesTable,
                                                                 Sqlite::ForeignKeyAction::NoAction,
                                                                 Sqlite::ForeignKeyAction::Restrict);
        typesTable.addColumn("prototypeNameId", Sqlite::StrictColumnType::Integer);
        auto &extensionIdColumn = typesTable.addForeignKeyColumn("extensionId",
                                                                 typesTable,
                                                                 Sqlite::ForeignKeyAction::NoAction,
                                                                 Sqlite::ForeignKeyAction::Restrict);
        typesTable.addColumn("extensionNameId", Sqlite::StrictColumnType::Integer);
        auto &defaultPropertyIdColumn = typesTable.addColumn("defaultPropertyId",
                                                             Sqlite::StrictColumnType::Integer);
        typesTable.addColumn("annotationTraits", Sqlite::StrictColumnType::Integer);
        typesTable.addUniqueIndex({sourceIdColumn, typesNameColumn});
        typesTable.addIndex({defaultPropertyIdColumn});
        typesTable.addIndex({prototypeIdColumn});
        typesTable.addIndex({extensionIdColumn});

        typesTable.initialize(database);

        {
            Sqlite::StrictTable propertyDeclarationTable;
            propertyDeclarationTable.setUseIfNotExists(true);
            propertyDeclarationTable.setName("propertyDeclarations");
            propertyDeclarationTable.addColumn("propertyDeclarationId",
                                               Sqlite::StrictColumnType::Integer,
                                               {Sqlite::PrimaryKey{}});
            auto &typeIdColumn = propertyDeclarationTable.addColumn("typeId");
            auto &nameColumn = propertyDeclarationTable.addColumn("name");
            auto &propertyTypeIdColumn = propertyDeclarationTable.addForeignKeyColumn(
                "propertyTypeId",
                typesTable,
                Sqlite::ForeignKeyAction::NoAction,
                Sqlite::ForeignKeyAction::Restrict);
            propertyDeclarationTable.addColumn("propertyTraits", Sqlite::StrictColumnType::Integer);
            propertyDeclarationTable.addColumn("propertyImportedTypeNameId",
                                               Sqlite::StrictColumnType::Integer);
            auto &aliasPropertyDeclarationIdColumn = propertyDeclarationTable.addForeignKeyColumn(
                "aliasPropertyDeclarationId",
                propertyDeclarationTable,
                Sqlite::ForeignKeyAction::NoAction,
                Sqlite::ForeignKeyAction::Restrict);
            auto &aliasPropertyDeclarationTailIdColumn = propertyDeclarationTable.addForeignKeyColumn(
                "aliasPropertyDeclarationTailId",
                propertyDeclarationTable,
                Sqlite::ForeignKeyAction::NoAction,
                Sqlite::ForeignKeyAction::Restrict);

            propertyDeclarationTable.addUniqueIndex({typeIdColumn, nameColumn});
            propertyDeclarationTable.addIndex({propertyTypeIdColumn});
            propertyDeclarationTable.addIndex({aliasPropertyDeclarationIdColumn},
                                              "aliasPropertyDeclarationId IS NOT NULL");
            propertyDeclarationTable.addIndex({aliasPropertyDeclarationTailIdColumn},
                                              "aliasPropertyDeclarationTailId IS NOT NULL");

            propertyDeclarationTable.initialize(database);
        }
    }

    void createExportedTypeNamesTable(Database &database,
                                      const Sqlite::StrictColumn &foreignModuleIdColumn)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("exportedTypeNames");
        table.addColumn("exportedTypeNameId",
                        Sqlite::StrictColumnType::Integer,
                        {Sqlite::PrimaryKey{}});
        auto &moduleIdColumn = table.addForeignKeyColumn("moduleId",
                                                         foreignModuleIdColumn,
                                                         Sqlite::ForeignKeyAction::NoAction,
                                                         Sqlite::ForeignKeyAction::NoAction);
        auto &nameColumn = table.addColumn("name", Sqlite::StrictColumnType::Text);
        auto &typeIdColumn = table.addColumn("typeId", Sqlite::StrictColumnType::Integer);
        auto &majorVersionColumn = table.addColumn("majorVersion", Sqlite::StrictColumnType::Integer);
        auto &minorVersionColumn = table.addColumn("minorVersion", Sqlite::StrictColumnType::Integer);

        table.addUniqueIndex({moduleIdColumn, nameColumn},
                             "majorVersion IS NULL AND minorVersion IS NULL");
        table.addUniqueIndex({moduleIdColumn, nameColumn, majorVersionColumn},
                             "majorVersion IS NOT NULL AND minorVersion IS NULL");
        table.addUniqueIndex({moduleIdColumn, nameColumn, majorVersionColumn, minorVersionColumn},
                             "majorVersion IS NOT NULL AND minorVersion IS NOT NULL");

        table.addIndex({typeIdColumn});
        table.addIndex({moduleIdColumn, nameColumn});

        table.initialize(database);
    }

    void createImportedTypeNamesTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("importedTypeNames");
        table.addColumn("importedTypeNameId",
                        Sqlite::StrictColumnType::Integer,
                        {Sqlite::PrimaryKey{}});
        auto &importOrSourceIdColumn = table.addColumn("importOrSourceId");
        auto &nameColumn = table.addColumn("name", Sqlite::StrictColumnType::Text);
        auto &kindColumn = table.addColumn("kind", Sqlite::StrictColumnType::Integer);

        table.addUniqueIndex({kindColumn, importOrSourceIdColumn, nameColumn});
        table.addIndex({nameColumn});

        table.initialize(database);
    }

    void createEnumerationsTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("enumerationDeclarations");
        table.addColumn("enumerationDeclarationId",
                        Sqlite::StrictColumnType::Integer,
                        {Sqlite::PrimaryKey{}});
        auto &typeIdColumn = table.addColumn("typeId", Sqlite::StrictColumnType::Integer);
        auto &nameColumn = table.addColumn("name", Sqlite::StrictColumnType::Text);
        table.addColumn("enumeratorDeclarations", Sqlite::StrictColumnType::Text);

        table.addUniqueIndex({typeIdColumn, nameColumn});

        table.initialize(database);
    }

    void createFunctionsTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("functionDeclarations");
        table.addColumn("functionDeclarationId",
                        Sqlite::StrictColumnType::Integer,
                        {Sqlite::PrimaryKey{}});
        auto &typeIdColumn = table.addColumn("typeId", Sqlite::StrictColumnType::Integer);
        auto &nameColumn = table.addColumn("name", Sqlite::StrictColumnType::Text);
        auto &signatureColumn = table.addColumn("signature", Sqlite::StrictColumnType::Text);
        table.addColumn("returnTypeName");

        table.addUniqueIndex({typeIdColumn, nameColumn, signatureColumn});

        table.initialize(database);
    }

    void createSignalsTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("signalDeclarations");
        table.addColumn("signalDeclarationId",
                        Sqlite::StrictColumnType::Integer,
                        {Sqlite::PrimaryKey{}});
        auto &typeIdColumn = table.addColumn("typeId", Sqlite::StrictColumnType::Integer);
        auto &nameColumn = table.addColumn("name", Sqlite::StrictColumnType::Text);
        auto &signatureColumn = table.addColumn("signature", Sqlite::StrictColumnType::Text);

        table.addUniqueIndex({typeIdColumn, nameColumn, signatureColumn});

        table.initialize(database);
    }

    Sqlite::StrictColumn createModulesTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("modules");
        auto &modelIdColumn = table.addColumn("moduleId",
                                              Sqlite::StrictColumnType::Integer,
                                              {Sqlite::PrimaryKey{}});
        auto &nameColumn = table.addColumn("name", Sqlite::StrictColumnType::Text);

        table.addUniqueIndex({nameColumn});

        table.initialize(database);

        return std::move(modelIdColumn);
    }

    void createModuleExportedImportsTable(Database &database,
                                          const Sqlite::StrictColumn &foreignModuleIdColumn)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("moduleExportedImports");
        table.addColumn("moduleExportedImportId",
                        Sqlite::StrictColumnType::Integer,
                        {Sqlite::PrimaryKey{}});
        auto &moduleIdColumn = table.addForeignKeyColumn("moduleId",
                                                         foreignModuleIdColumn,
                                                         Sqlite::ForeignKeyAction::NoAction,
                                                         Sqlite::ForeignKeyAction::Cascade,
                                                         Sqlite::Enforment::Immediate);
        auto &sourceIdColumn = table.addColumn("exportedModuleId", Sqlite::StrictColumnType::Integer);
        table.addColumn("isAutoVersion", Sqlite::StrictColumnType::Integer);
        table.addColumn("majorVersion", Sqlite::StrictColumnType::Integer);
        table.addColumn("minorVersion", Sqlite::StrictColumnType::Integer);

        table.addUniqueIndex({sourceIdColumn, moduleIdColumn});

        table.initialize(database);
    }

    void createDocumentImportsTable(Database &database,
                                    const Sqlite::StrictColumn &foreignModuleIdColumn)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("documentImports");
        table.addColumn("importId", Sqlite::StrictColumnType::Integer, {Sqlite::PrimaryKey{}});
        auto &sourceIdColumn = table.addColumn("sourceId", Sqlite::StrictColumnType::Integer);
        auto &moduleIdColumn = table.addForeignKeyColumn("moduleId",
                                                         foreignModuleIdColumn,
                                                         Sqlite::ForeignKeyAction::NoAction,
                                                         Sqlite::ForeignKeyAction::Cascade,
                                                         Sqlite::Enforment::Immediate);
        auto &sourceModuleIdColumn = table.addForeignKeyColumn("sourceModuleId",
                                                               foreignModuleIdColumn,
                                                               Sqlite::ForeignKeyAction::NoAction,
                                                               Sqlite::ForeignKeyAction::Cascade,
                                                               Sqlite::Enforment::Immediate);
        auto &kindColumn = table.addColumn("kind", Sqlite::StrictColumnType::Integer);
        auto &majorVersionColumn = table.addColumn("majorVersion", Sqlite::StrictColumnType::Integer);
        auto &minorVersionColumn = table.addColumn("minorVersion", Sqlite::StrictColumnType::Integer);
        auto &parentImportIdColumn = table.addColumn("parentImportId",
                                                     Sqlite::StrictColumnType::Integer);

        table.addUniqueIndex(
            {sourceIdColumn, moduleIdColumn, kindColumn, sourceModuleIdColumn, parentImportIdColumn},
            "majorVersion IS NULL AND minorVersion IS NULL");
        table.addUniqueIndex({sourceIdColumn,
                              moduleIdColumn,
                              kindColumn,
                              sourceModuleIdColumn,
                              majorVersionColumn,
                              parentImportIdColumn},
                             "majorVersion IS NOT NULL AND minorVersion IS NULL");
        table.addUniqueIndex({sourceIdColumn,
                              moduleIdColumn,
                              kindColumn,
                              sourceModuleIdColumn,
                              majorVersionColumn,
                              minorVersionColumn,
                              parentImportIdColumn},
                             "majorVersion IS NOT NULL AND minorVersion IS NOT NULL");

        table.addIndex({sourceIdColumn, kindColumn});

        table.initialize(database);
    }

    void createFileStatusesTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setName("fileStatuses");
        table.addColumn("sourceId",
                        Sqlite::StrictColumnType::Integer,
                        {Sqlite::PrimaryKey{},
                         Sqlite::ForeignKey{"sources",
                                            "sourceId",
                                            Sqlite::ForeignKeyAction::NoAction,
                                            Sqlite::ForeignKeyAction::Cascade}});
        table.addColumn("size", Sqlite::StrictColumnType::Integer);
        table.addColumn("lastModified", Sqlite::StrictColumnType::Integer);

        table.initialize(database);
    }

    void createProjectDatasTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setUseWithoutRowId(true);
        table.setName("projectDatas");
        auto &projectSourceIdColumn = table.addColumn("projectSourceId",
                                                      Sqlite::StrictColumnType::Integer);
        auto &sourceIdColumn = table.addColumn("sourceId", Sqlite::StrictColumnType::Integer);
        table.addColumn("moduleId", Sqlite::StrictColumnType::Integer);
        table.addColumn("fileType", Sqlite::StrictColumnType::Integer);

        table.addPrimaryKeyContraint({projectSourceIdColumn, sourceIdColumn});
        table.addUniqueIndex({sourceIdColumn});

        table.initialize(database);
    }

    void createPropertyEditorPathsTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setUseWithoutRowId(true);
        table.setName("propertyEditorPaths");
        table.addColumn("typeId", Sqlite::StrictColumnType::Integer, {Sqlite::PrimaryKey{}});
        table.addColumn("pathSourceId", Sqlite::StrictColumnType::Integer);
        auto &directoryIdColumn = table.addColumn("directoryId", Sqlite::StrictColumnType::Integer);

        table.addIndex({directoryIdColumn});

        table.initialize(database);
    }

    void createTypeAnnotionsTable(Database &database)
    {
        Sqlite::StrictTable table;
        table.setUseIfNotExists(true);
        table.setUseWithoutRowId(true);
        table.setName("typeAnnotations");
        auto &typeIdColumn = table.addColumn("typeId",
                                             Sqlite::StrictColumnType::Integer,
                                             {Sqlite::PrimaryKey{}});
        auto &sourceIdColumn = table.addColumn("sourceId", Sqlite::StrictColumnType::Integer);
        auto &directorySourceIdColumn = table.addColumn("directorySourceId",
                                                        Sqlite::StrictColumnType::Integer);

        table.addColumn("iconPath", Sqlite::StrictColumnType::Text);
        table.addColumn("itemLibrary", Sqlite::StrictColumnType::Text);
        table.addColumn("hints", Sqlite::StrictColumnType::Text);

        table.addUniqueIndex({sourceIdColumn, typeIdColumn});
        table.addIndex({directorySourceIdColumn});

        table.initialize(database);
    }
};

ProjectStorage::ProjectStorage(Database &database, bool isInitialized)
    : database{database}
    , exclusiveTransaction{database}
    , initializer{std::make_unique<ProjectStorage::Initializer>(database, isInitialized)}
    , moduleCache{ModuleStorageAdapter{*this}}
    , s{std::make_unique<ProjectStorage::Statements>(database)}
{
    NanotraceHR::Tracer tracer{"initialize"_t, projectStorageCategory()};

    exclusiveTransaction.commit();

    database.walCheckpointFull();

    moduleCache.populate();
}

ProjectStorage::~ProjectStorage() = default;

void ProjectStorage::synchronize(Storage::Synchronization::SynchronizationPackage package)
{
    NanotraceHR::Tracer tracer{"synchronize"_t, projectStorageCategory()};

    TypeIds deletedTypeIds;
    Sqlite::withImmediateTransaction(database, [&] {
        AliasPropertyDeclarations insertedAliasPropertyDeclarations;
        AliasPropertyDeclarations updatedAliasPropertyDeclarations;

        AliasPropertyDeclarations relinkableAliasPropertyDeclarations;
        PropertyDeclarations relinkablePropertyDeclarations;
        Prototypes relinkablePrototypes;
        Prototypes relinkableExtensions;

        TypeIds updatedTypeIds;
        updatedTypeIds.reserve(package.types.size());

        TypeIds typeIdsToBeDeleted;

        std::sort(package.updatedSourceIds.begin(), package.updatedSourceIds.end());

        synchronizeFileStatuses(package.fileStatuses, package.updatedFileStatusSourceIds);
        synchronizeImports(package.imports,
                           package.updatedSourceIds,
                           package.moduleDependencies,
                           package.updatedModuleDependencySourceIds,
                           package.moduleExportedImports,
                           package.updatedModuleIds);
        synchronizeTypes(package.types,
                         updatedTypeIds,
                         insertedAliasPropertyDeclarations,
                         updatedAliasPropertyDeclarations,
                         relinkableAliasPropertyDeclarations,
                         relinkablePropertyDeclarations,
                         relinkablePrototypes,
                         relinkableExtensions,
                         package.updatedSourceIds);
        synchronizeTypeAnnotations(package.typeAnnotations, package.updatedTypeAnnotationSourceIds);
        synchronizePropertyEditorQmlPaths(package.propertyEditorQmlPaths,
                                          package.updatedPropertyEditorQmlPathSourceIds);

        deleteNotUpdatedTypes(updatedTypeIds,
                              package.updatedSourceIds,
                              typeIdsToBeDeleted,
                              relinkableAliasPropertyDeclarations,
                              relinkablePropertyDeclarations,
                              relinkablePrototypes,
                              relinkableExtensions,
                              deletedTypeIds);

        relink(relinkableAliasPropertyDeclarations,
               relinkablePropertyDeclarations,
               relinkablePrototypes,
               relinkableExtensions,
               deletedTypeIds);

        linkAliases(insertedAliasPropertyDeclarations, updatedAliasPropertyDeclarations);

        synchronizeProjectDatas(package.projectDatas, package.updatedProjectSourceIds);

        commonTypeCache_.resetTypeIds();
    });

    callRefreshMetaInfoCallback(deletedTypeIds);
}

void ProjectStorage::synchronizeDocumentImports(Storage::Imports imports, SourceId sourceId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"synchronize document imports"_t,
                               projectStorageCategory(),
                               keyValue("imports", imports),
                               keyValue("source id", sourceId)};

    Sqlite::withImmediateTransaction(database, [&] {
        synchronizeDocumentImports(imports, {sourceId}, Storage::Synchronization::ImportKind::Import);
    });
}

void ProjectStorage::addObserver(ProjectStorageObserver *observer)
{
    NanotraceHR::Tracer tracer{"add observer"_t, projectStorageCategory()};
    observers.push_back(observer);
}

void ProjectStorage::removeObserver(ProjectStorageObserver *observer)
{
    NanotraceHR::Tracer tracer{"remove observer"_t, projectStorageCategory()};
    observers.removeOne(observer);
}

ModuleId ProjectStorage::moduleId(Utils::SmallStringView moduleName) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get module id"_t,
                               projectStorageCategory(),
                               keyValue("module name", moduleName)};

    auto moduleId = moduleCache.id(moduleName);

    tracer.end(keyValue("module id", moduleId));

    return moduleId;
}

Utils::SmallString ProjectStorage::moduleName(ModuleId moduleId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get module name"_t,
                               projectStorageCategory(),
                               keyValue("module id", moduleId)};

    if (!moduleId)
        throw ModuleDoesNotExists{};

    auto moduleName = moduleCache.value(moduleId);

    tracer.end(keyValue("module name", moduleName));

    return moduleName;
}

TypeId ProjectStorage::typeId(ModuleId moduleId,
                              Utils::SmallStringView exportedTypeName,
                              Storage::Version version) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get type id by exported name"_t,
                               projectStorageCategory(),
                               keyValue("module id", moduleId),
                               keyValue("exported type name", exportedTypeName),
                               keyValue("version", version)};

    TypeId typeId;

    if (version.minor) {
        typeId = s->selectTypeIdByModuleIdAndExportedNameAndVersionStatement.valueWithTransaction<TypeId>(
            moduleId, exportedTypeName, version.major.value, version.minor.value);

    } else if (version.major) {
        typeId = s->selectTypeIdByModuleIdAndExportedNameAndMajorVersionStatement
                     .valueWithTransaction<TypeId>(moduleId, exportedTypeName, version.major.value);

    } else {
        typeId = s->selectTypeIdByModuleIdAndExportedNameStatement
                     .valueWithTransaction<TypeId>(moduleId, exportedTypeName);
    }

    tracer.end(keyValue("type id", typeId));

    return typeId;
}

TypeId ProjectStorage::typeId(ImportedTypeNameId typeNameId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get type id by imported type name"_t,
                               projectStorageCategory(),
                               keyValue("imported type name id", typeNameId)};

    auto typeId = Sqlite::withDeferredTransaction(database, [&] { return fetchTypeId(typeNameId); });

    tracer.end(keyValue("type id", typeId));

    return typeId;
}

QVarLengthArray<TypeId, 256> ProjectStorage::typeIds(ModuleId moduleId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get type ids by module id"_t,
                               projectStorageCategory(),
                               keyValue("module id", moduleId)};

    auto typeIds = s->selectTypeIdsByModuleIdStatement
                       .valuesWithTransaction<QVarLengthArray<TypeId, 256>>(moduleId);

    tracer.end(keyValue("type ids", typeIds));

    return typeIds;
}

Storage::Info::ExportedTypeNames ProjectStorage::exportedTypeNames(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get exported type names by type id"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto exportedTypenames = s->selectExportedTypesByTypeIdStatement
                                 .valuesWithTransaction<Storage::Info::ExportedTypeName, 4>(typeId);

    tracer.end(keyValue("exported type names", exportedTypenames));

    return exportedTypenames;
}

Storage::Info::ExportedTypeNames ProjectStorage::exportedTypeNames(TypeId typeId, SourceId sourceId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get exported type names by source id"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("source id", sourceId)};

    auto exportedTypenames = s->selectExportedTypesByTypeIdAndSourceIdStatement
                                 .valuesWithTransaction<Storage::Info::ExportedTypeName, 4>(typeId,
                                                                                            sourceId);

    tracer.end(keyValue("exported type names", exportedTypenames));

    return exportedTypenames;
}

ImportId ProjectStorage::importId(const Storage::Import &import) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get import id by import"_t,
                               projectStorageCategory(),
                               keyValue("import", import)};

    auto importId = Sqlite::withDeferredTransaction(database, [&] {
        return fetchImportId(import.sourceId, import);
    });

    tracer.end(keyValue("import id", importId));

    return importId;
}

ImportedTypeNameId ProjectStorage::importedTypeNameId(ImportId importId,
                                                      Utils::SmallStringView typeName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get imported type name id by import id"_t,
                               projectStorageCategory(),
                               keyValue("import id", importId),
                               keyValue("imported type name", typeName)};

    auto importedTypeNameId = Sqlite::withDeferredTransaction(database, [&] {
        return fetchImportedTypeNameId(Storage::Synchronization::TypeNameKind::QualifiedExported,
                                       importId,
                                       typeName);
    });

    tracer.end(keyValue("imported type name id", importedTypeNameId));

    return importedTypeNameId;
}

ImportedTypeNameId ProjectStorage::importedTypeNameId(SourceId sourceId,
                                                      Utils::SmallStringView typeName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get imported type name id by source id"_t,
                               projectStorageCategory(),
                               keyValue("source id", sourceId),
                               keyValue("imported type name", typeName)};

    auto importedTypeNameId = Sqlite::withDeferredTransaction(database, [&] {
        return fetchImportedTypeNameId(Storage::Synchronization::TypeNameKind::Exported,
                                       sourceId,
                                       typeName);
    });

    tracer.end(keyValue("imported type name id", importedTypeNameId));

    return importedTypeNameId;
}

QVarLengthArray<PropertyDeclarationId, 128> ProjectStorage::propertyDeclarationIds(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get property declaration ids"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto propertyDeclarationIds = Sqlite::withDeferredTransaction(database, [&] {
        return fetchPropertyDeclarationIds(typeId);
    });

    std::sort(propertyDeclarationIds.begin(), propertyDeclarationIds.end());

    tracer.end(keyValue("property declaration ids", propertyDeclarationIds));

    return propertyDeclarationIds;
}

QVarLengthArray<PropertyDeclarationId, 128> ProjectStorage::localPropertyDeclarationIds(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get local property declaration ids"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto propertyDeclarationIds = s->selectLocalPropertyDeclarationIdsForTypeStatement
                                      .valuesWithTransaction<QVarLengthArray<PropertyDeclarationId, 128>>(
                                          typeId);

    tracer.end(keyValue("property declaration ids", propertyDeclarationIds));

    return propertyDeclarationIds;
}

PropertyDeclarationId ProjectStorage::propertyDeclarationId(TypeId typeId,
                                                            Utils::SmallStringView propertyName) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get property declaration id"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("property name", propertyName)};

    auto propertyDeclarationId = Sqlite::withDeferredTransaction(database, [&] {
        return fetchPropertyDeclarationId(typeId, propertyName);
    });

    tracer.end(keyValue("property declaration id", propertyDeclarationId));

    return propertyDeclarationId;
}

PropertyDeclarationId ProjectStorage::localPropertyDeclarationId(TypeId typeId,
                                                                 Utils::SmallStringView propertyName) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get local property declaration id"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("property name", propertyName)};

    auto propertyDeclarationId = s->selectLocalPropertyDeclarationIdForTypeAndPropertyNameStatement
                                     .valueWithTransaction<PropertyDeclarationId>(typeId,
                                                                                  propertyName);

    tracer.end(keyValue("property declaration id", propertyDeclarationId));

    return propertyDeclarationId;
}

PropertyDeclarationId ProjectStorage::defaultPropertyDeclarationId(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get default property declaration id"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto propertyDeclarationId = Sqlite::withDeferredTransaction(database, [&] {
        return fetchDefaultPropertyDeclarationId(typeId);
    });

    tracer.end(keyValue("property declaration id", propertyDeclarationId));

    return propertyDeclarationId;
}

std::optional<Storage::Info::PropertyDeclaration> ProjectStorage::propertyDeclaration(
    PropertyDeclarationId propertyDeclarationId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get property declaration"_t,
                               projectStorageCategory(),
                               keyValue("property declaration id", propertyDeclarationId)};

    auto propertyDeclaration = s->selectPropertyDeclarationForPropertyDeclarationIdStatement
                                   .optionalValueWithTransaction<Storage::Info::PropertyDeclaration>(
                                       propertyDeclarationId);

    tracer.end(keyValue("property declaration", propertyDeclaration));

    return propertyDeclaration;
}

std::optional<Storage::Info::Type> ProjectStorage::type(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get type"_t, projectStorageCategory(), keyValue("type id", typeId)};

    auto type = s->selectInfoTypeByTypeIdStatement.optionalValueWithTransaction<Storage::Info::Type>(
        typeId);

    tracer.end(keyValue("type", type));

    return type;
}

Utils::PathString ProjectStorage::typeIconPath(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get type icon path"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto typeIconPath = s->selectTypeIconPathStatement.valueWithTransaction<Utils::PathString>(typeId);

    tracer.end(keyValue("type icon path", typeIconPath));

    return typeIconPath;
}

Storage::Info::TypeHints ProjectStorage::typeHints(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get type hints"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto typeHints = s->selectTypeHintsStatement.valuesWithTransaction<Storage::Info::TypeHints, 4>(
        typeId);

    tracer.end(keyValue("type hints", typeHints));

    return typeHints;
}

SmallSourceIds<4> ProjectStorage::typeAnnotationSourceIds(SourceId directoryId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get type annotaion source ids"_t,
                               projectStorageCategory(),
                               keyValue("source id", directoryId)};

    auto sourceIds = s->selectTypeAnnotationSourceIdsStatement.valuesWithTransaction<SmallSourceIds<4>>(
        directoryId);

    tracer.end(keyValue("source ids", sourceIds));

    return sourceIds;
}

SmallSourceIds<64> ProjectStorage::typeAnnotationDirectorySourceIds() const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get type annotaion source ids"_t, projectStorageCategory()};

    auto sourceIds = s->selectTypeAnnotationDirectorySourceIdsStatement
                         .valuesWithTransaction<SmallSourceIds<64>>();

    tracer.end(keyValue("source ids", sourceIds));

    return sourceIds;
}

Storage::Info::ItemLibraryEntries ProjectStorage::itemLibraryEntries(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get item library entries  by type id"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    using Storage::Info::ItemLibraryProperties;
    Storage::Info::ItemLibraryEntries entries;

    auto callback = [&](TypeId typeId_,
                        Utils::SmallStringView name,
                        Utils::SmallStringView iconPath,
                        Utils::SmallStringView category,
                        Utils::SmallStringView import,
                        Utils::SmallStringView toolTip,
                        Utils::SmallStringView properties,
                        Utils::SmallStringView extraFilePaths,
                        Utils::SmallStringView templatePath) {
        auto &last = entries.emplace_back(typeId_, name, iconPath, category, import, toolTip, templatePath);
        if (properties.size())
            s->selectItemLibraryPropertiesStatement.readTo(last.properties, properties);
        if (extraFilePaths.size())
            s->selectItemLibraryExtraFilePathsStatement.readTo(last.extraFilePaths, extraFilePaths);
    };

    s->selectItemLibraryEntriesByTypeIdStatement.readCallbackWithTransaction(callback, typeId);

    tracer.end(keyValue("item library entries", entries));

    return entries;
}

Storage::Info::ItemLibraryEntries ProjectStorage::itemLibraryEntries(ImportId importId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get item library entries  by import id"_t,
                               projectStorageCategory(),
                               keyValue("import id", importId)};

    using Storage::Info::ItemLibraryProperties;
    Storage::Info::ItemLibraryEntries entries;

    auto callback = [&](TypeId typeId_,
                        Utils::SmallStringView name,
                        Utils::SmallStringView iconPath,
                        Utils::SmallStringView category,
                        Utils::SmallStringView import,
                        Utils::SmallStringView toolTip,
                        Utils::SmallStringView properties,
                        Utils::SmallStringView extraFilePaths,
                        Utils::SmallStringView templatePath) {
        auto &last = entries.emplace_back(typeId_, name, iconPath, category, import, toolTip, templatePath);
        if (properties.size())
            s->selectItemLibraryPropertiesStatement.readTo(last.properties, properties);
        if (extraFilePaths.size())
            s->selectItemLibraryExtraFilePathsStatement.readTo(last.extraFilePaths, extraFilePaths);
    };

    s->selectItemLibraryEntriesByTypeIdStatement.readCallbackWithTransaction(callback, importId);

    tracer.end(keyValue("item library entries", entries));

    return entries;
}

Storage::Info::ItemLibraryEntries ProjectStorage::itemLibraryEntries(SourceId sourceId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get item library entries by source id"_t,
                               projectStorageCategory(),
                               keyValue("source id", sourceId)};

    using Storage::Info::ItemLibraryProperties;
    Storage::Info::ItemLibraryEntries entries;

    auto callback = [&](TypeId typeId,
                        Utils::SmallStringView name,
                        Utils::SmallStringView iconPath,
                        Utils::SmallStringView category,
                        Utils::SmallStringView import,
                        Utils::SmallStringView toolTip,
                        Utils::SmallStringView properties,
                        Utils::SmallStringView extraFilePaths,
                        Utils::SmallStringView templatePath) {
        auto &last = entries.emplace_back(typeId, name, iconPath, category, import, toolTip, templatePath);
        if (properties.size())
            s->selectItemLibraryPropertiesStatement.readTo(last.properties, properties);
        if (extraFilePaths.size())
            s->selectItemLibraryExtraFilePathsStatement.readTo(last.extraFilePaths, extraFilePaths);
    };

    s->selectItemLibraryEntriesBySourceIdStatement.readCallbackWithTransaction(callback, sourceId);

    tracer.end(keyValue("item library entries", entries));

    return entries;
}

Storage::Info::ItemLibraryEntries ProjectStorage::allItemLibraryEntries() const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get all item library entries"_t, projectStorageCategory()};

    using Storage::Info::ItemLibraryProperties;
    Storage::Info::ItemLibraryEntries entries;

    auto callback = [&](TypeId typeId,
                        Utils::SmallStringView name,
                        Utils::SmallStringView iconPath,
                        Utils::SmallStringView category,
                        Utils::SmallStringView import,
                        Utils::SmallStringView toolTip,
                        Utils::SmallStringView properties,
                        Utils::SmallStringView extraFilePaths,
                        Utils::SmallStringView templatePath) {
        auto &last = entries.emplace_back(typeId, name, iconPath, category, import, toolTip, templatePath);
        if (properties.size())
            s->selectItemLibraryPropertiesStatement.readTo(last.properties, properties);
        if (extraFilePaths.size())
            s->selectItemLibraryExtraFilePathsStatement.readTo(last.extraFilePaths, extraFilePaths);
    };

    s->selectItemLibraryEntriesStatement.readCallbackWithTransaction(callback);

    tracer.end(keyValue("item library entries", entries));

    return entries;
}

std::vector<Utils::SmallString> ProjectStorage::signalDeclarationNames(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get signal names"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto signalDeclarationNames = s->selectSignalDeclarationNamesForTypeStatement
                                      .valuesWithTransaction<Utils::SmallString, 32>(typeId);

    tracer.end(keyValue("signal names", signalDeclarationNames));

    return signalDeclarationNames;
}

std::vector<Utils::SmallString> ProjectStorage::functionDeclarationNames(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get function names"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto functionDeclarationNames = s->selectFuncionDeclarationNamesForTypeStatement
                                        .valuesWithTransaction<Utils::SmallString, 32>(typeId);

    tracer.end(keyValue("function names", functionDeclarationNames));

    return functionDeclarationNames;
}

std::optional<Utils::SmallString> ProjectStorage::propertyName(
    PropertyDeclarationId propertyDeclarationId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get property name"_t,
                               projectStorageCategory(),
                               keyValue("property declaration id", propertyDeclarationId)};

    auto propertyName = s->selectPropertyNameStatement.optionalValueWithTransaction<Utils::SmallString>(
        propertyDeclarationId);

    tracer.end(keyValue("property name", propertyName));

    return propertyName;
}

SmallTypeIds<16> ProjectStorage::prototypeIds(TypeId type) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get prototypes"_t, projectStorageCategory(), keyValue("type id", type)};

    auto prototypeIds = s->selectPrototypeAndExtensionIdsStatement
                            .valuesWithTransaction<SmallTypeIds<16>>(type);

    tracer.end(keyValue("type ids", prototypeIds));

    return prototypeIds;
}

SmallTypeIds<16> ProjectStorage::prototypeAndSelfIds(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get prototypes and self"_t, projectStorageCategory()};

    SmallTypeIds<16> prototypeAndSelfIds;
    prototypeAndSelfIds.push_back(typeId);

    s->selectPrototypeAndExtensionIdsStatement.readToWithTransaction(prototypeAndSelfIds, typeId);

    tracer.end(keyValue("type ids", prototypeAndSelfIds));

    return prototypeAndSelfIds;
}

SmallTypeIds<64> ProjectStorage::heirIds(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get heirs"_t, projectStorageCategory()};

    auto heirIds = s->selectHeirTypeIdsStatement.valuesWithTransaction<SmallTypeIds<64>>(typeId);

    tracer.end(keyValue("type ids", heirIds));

    return heirIds;
}

bool ProjectStorage::isBasedOn(TypeId) const
{
    return false;
}

bool ProjectStorage::isBasedOn(TypeId typeId, TypeId id1) const
{
    return isBasedOn_(typeId, id1);
}

bool ProjectStorage::isBasedOn(TypeId typeId, TypeId id1, TypeId id2) const
{
    return isBasedOn_(typeId, id1, id2);
}

bool ProjectStorage::isBasedOn(TypeId typeId, TypeId id1, TypeId id2, TypeId id3) const
{
    return isBasedOn_(typeId, id1, id2, id3);
}

bool ProjectStorage::isBasedOn(TypeId typeId, TypeId id1, TypeId id2, TypeId id3, TypeId id4) const
{
    return isBasedOn_(typeId, id1, id2, id3, id4);
}

bool ProjectStorage::isBasedOn(TypeId typeId, TypeId id1, TypeId id2, TypeId id3, TypeId id4, TypeId id5) const
{
    return isBasedOn_(typeId, id1, id2, id3, id4, id5);
}

bool ProjectStorage::isBasedOn(
    TypeId typeId, TypeId id1, TypeId id2, TypeId id3, TypeId id4, TypeId id5, TypeId id6) const
{
    return isBasedOn_(typeId, id1, id2, id3, id4, id5, id6);
}

bool ProjectStorage::isBasedOn(
    TypeId typeId, TypeId id1, TypeId id2, TypeId id3, TypeId id4, TypeId id5, TypeId id6, TypeId id7) const
{
    return isBasedOn_(typeId, id1, id2, id3, id4, id5, id6, id7);
}

TypeId ProjectStorage::fetchTypeIdByExportedName(Utils::SmallStringView name) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on"_t,
                               projectStorageCategory(),
                               keyValue("exported type name", name)};

    auto typeId = s->selectTypeIdByExportedNameStatement.valueWithTransaction<TypeId>(name);

    tracer.end(keyValue("type id", typeId));

    return typeId;
}

TypeId ProjectStorage::fetchTypeIdByModuleIdsAndExportedName(ModuleIds moduleIds,
                                                             Utils::SmallStringView name) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch type id by module ids and exported name"_t,
                               projectStorageCategory(),
                               keyValue("module ids", NanotraceHR::array(moduleIds)),
                               keyValue("exported type name", name)};
    auto typeId = s->selectTypeIdByModuleIdsAndExportedNameStatement.valueWithTransaction<TypeId>(
        static_cast<void *>(moduleIds.data()), static_cast<long long>(moduleIds.size()), name);

    tracer.end(keyValue("type id", typeId));

    return typeId;
}

TypeId ProjectStorage::fetchTypeIdByName(SourceId sourceId, Utils::SmallStringView name)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch type id by name"_t,
                               projectStorageCategory(),
                               keyValue("source id", sourceId),
                               keyValue("internal type name", name)};

    auto typeId = s->selectTypeIdBySourceIdAndNameStatement.valueWithTransaction<TypeId>(sourceId,
                                                                                         name);

    tracer.end(keyValue("type id", typeId));

    return typeId;
}

Storage::Synchronization::Type ProjectStorage::fetchTypeByTypeId(TypeId typeId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch type by type id"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto type = Sqlite::withDeferredTransaction(database, [&] {
        auto type = s->selectTypeByTypeIdStatement.value<Storage::Synchronization::Type>(typeId);

        type.exportedTypes = fetchExportedTypes(typeId);
        type.propertyDeclarations = fetchPropertyDeclarations(type.typeId);
        type.functionDeclarations = fetchFunctionDeclarations(type.typeId);
        type.signalDeclarations = fetchSignalDeclarations(type.typeId);
        type.enumerationDeclarations = fetchEnumerationDeclarations(type.typeId);

        return type;
    });

    tracer.end(keyValue("type", type));

    return type;
}

Storage::Synchronization::Types ProjectStorage::fetchTypes()
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch types"_t, projectStorageCategory()};

    auto types = Sqlite::withDeferredTransaction(database, [&] {
        auto types = s->selectTypesStatement.values<Storage::Synchronization::Type, 64>();

        for (Storage::Synchronization::Type &type : types) {
            type.exportedTypes = fetchExportedTypes(type.typeId);
            type.propertyDeclarations = fetchPropertyDeclarations(type.typeId);
            type.functionDeclarations = fetchFunctionDeclarations(type.typeId);
            type.signalDeclarations = fetchSignalDeclarations(type.typeId);
            type.enumerationDeclarations = fetchEnumerationDeclarations(type.typeId);
        }

        return types;
    });

    tracer.end(keyValue("type", types));

    return types;
}

SourceContextId ProjectStorage::fetchSourceContextIdUnguarded(Utils::SmallStringView sourceContextPath)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch source context id unguarded"_t, projectStorageCategory()};

    auto sourceContextId = readSourceContextId(sourceContextPath);

    return sourceContextId ? sourceContextId : writeSourceContextId(sourceContextPath);
}

SourceContextId ProjectStorage::fetchSourceContextId(Utils::SmallStringView sourceContextPath)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch source context id"_t,
                               projectStorageCategory(),
                               keyValue("source context path", sourceContextPath)};

    SourceContextId sourceContextId;
    try {
        sourceContextId = Sqlite::withDeferredTransaction(database, [&] {
            return fetchSourceContextIdUnguarded(sourceContextPath);
        });
    } catch (const Sqlite::ConstraintPreventsModification &) {
        sourceContextId = fetchSourceContextId(sourceContextPath);
    }

    tracer.end(keyValue("source context id", sourceContextId));

    return sourceContextId;
}

Utils::PathString ProjectStorage::fetchSourceContextPath(SourceContextId sourceContextId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch source context path"_t,
                               projectStorageCategory(),
                               keyValue("source context id", sourceContextId)};

    auto path = Sqlite::withDeferredTransaction(database, [&] {
        auto optionalSourceContextPath = s->selectSourceContextPathFromSourceContextsBySourceContextIdStatement
                                             .optionalValue<Utils::PathString>(sourceContextId);

        if (!optionalSourceContextPath)
            throw SourceContextIdDoesNotExists();

        return std::move(*optionalSourceContextPath);
    });

    tracer.end(keyValue("source context path", path));

    return path;
}

Cache::SourceContexts ProjectStorage::fetchAllSourceContexts() const
{
    NanotraceHR::Tracer tracer{"fetch all source contexts"_t, projectStorageCategory()};

    return s->selectAllSourceContextsStatement.valuesWithTransaction<Cache::SourceContext, 128>();
}

SourceId ProjectStorage::fetchSourceId(SourceContextId sourceContextId,
                                       Utils::SmallStringView sourceName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch source id"_t,
                               projectStorageCategory(),
                               keyValue("source context id", sourceContextId),
                               keyValue("source name", sourceName)};

    auto sourceId = Sqlite::withDeferredTransaction(database, [&] {
        return fetchSourceIdUnguarded(sourceContextId, sourceName);
    });

    tracer.end(keyValue("source id", sourceId));

    return sourceId;
}

Cache::SourceNameAndSourceContextId ProjectStorage::fetchSourceNameAndSourceContextId(SourceId sourceId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch source name and source context id"_t,
                               projectStorageCategory(),
                               keyValue("source id", sourceId)};

    auto value = s->selectSourceNameAndSourceContextIdFromSourcesBySourceIdStatement
                     .valueWithTransaction<Cache::SourceNameAndSourceContextId>(sourceId);

    if (!value.sourceContextId)
        throw SourceIdDoesNotExists();

    tracer.end(keyValue("source name", value.sourceName),
               keyValue("source context id", value.sourceContextId));

    return value;
}

void ProjectStorage::clearSources()
{
    Sqlite::withImmediateTransaction(database, [&] {
        s->deleteAllSourceContextsStatement.execute();
        s->deleteAllSourcesStatement.execute();
    });
}

SourceContextId ProjectStorage::fetchSourceContextId(SourceId sourceId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch source context id"_t,
                               projectStorageCategory(),
                               keyValue("source id", sourceId)};

    auto sourceContextId = s->selectSourceContextIdFromSourcesBySourceIdStatement
                               .valueWithTransaction<SourceContextId>(sourceId);

    if (!sourceContextId)
        throw SourceIdDoesNotExists();

    tracer.end(keyValue("source context id", sourceContextId));

    return sourceContextId;
}

Cache::Sources ProjectStorage::fetchAllSources() const
{
    NanotraceHR::Tracer tracer{"fetch all sources"_t, projectStorageCategory()};

    return s->selectAllSourcesStatement.valuesWithTransaction<Cache::Source, 1024>();
}

SourceId ProjectStorage::fetchSourceIdUnguarded(SourceContextId sourceContextId,
                                                Utils::SmallStringView sourceName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch source id unguarded"_t,
                               projectStorageCategory(),
                               keyValue("source context id", sourceContextId),
                               keyValue("source name", sourceName)};

    auto sourceId = readSourceId(sourceContextId, sourceName);

    if (!sourceId)
        sourceId = writeSourceId(sourceContextId, sourceName);

    tracer.end(keyValue("source id", sourceId));

    return sourceId;
}

FileStatuses ProjectStorage::fetchAllFileStatuses() const
{
    NanotraceHR::Tracer tracer{"fetch all file statuses"_t, projectStorageCategory()};

    return s->selectAllFileStatusesStatement.valuesWithTransaction<FileStatus>();
}

FileStatus ProjectStorage::fetchFileStatus(SourceId sourceId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch file status"_t,
                               projectStorageCategory(),
                               keyValue("source id", sourceId)};

    auto fileStatus = s->selectFileStatusesForSourceIdStatement.valueWithTransaction<FileStatus>(
        sourceId);

    tracer.end(keyValue("file status", fileStatus));

    return fileStatus;
}

std::optional<Storage::Synchronization::ProjectData> ProjectStorage::fetchProjectData(SourceId sourceId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch project data"_t,
                               projectStorageCategory(),
                               keyValue("source id", sourceId)};

    auto projectData = s->selectProjectDataForSourceIdStatement
                           .optionalValueWithTransaction<Storage::Synchronization::ProjectData>(
                               sourceId);

    tracer.end(keyValue("project data", projectData));

    return projectData;
}

Storage::Synchronization::ProjectDatas ProjectStorage::fetchProjectDatas(SourceId projectSourceId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch project datas by source id"_t,
                               projectStorageCategory(),
                               keyValue("source id", projectSourceId)};

    auto projectDatas = s->selectProjectDatasForSourceIdStatement
                            .valuesWithTransaction<Storage::Synchronization::ProjectData, 1024>(
                                projectSourceId);

    tracer.end(keyValue("project datas", projectDatas));

    return projectDatas;
}

Storage::Synchronization::ProjectDatas ProjectStorage::fetchProjectDatas(
    const SourceIds &projectSourceIds) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch project datas by source ids"_t,
                               projectStorageCategory(),
                               keyValue("source ids", projectSourceIds)};

    auto projectDatas = s->selectProjectDatasForSourceIdsStatement
                            .valuesWithTransaction<Storage::Synchronization::ProjectData, 64>(
                                toIntegers(projectSourceIds));

    tracer.end(keyValue("project datas", projectDatas));

    return projectDatas;
}

void ProjectStorage::setPropertyEditorPathId(TypeId typeId, SourceId pathId)
{
    Sqlite::ImmediateSessionTransaction transaction{database};

    s->upsertPropertyEditorPathIdStatement.write(typeId, pathId);

    transaction.commit();
}

SourceId ProjectStorage::propertyEditorPathId(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"property editor path id"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto sourceId = s->selectPropertyEditorPathIdStatement.valueWithTransaction<SourceId>(typeId);

    tracer.end(keyValue("source id", sourceId));

    return sourceId;
}

Storage::Imports ProjectStorage::fetchDocumentImports() const
{
    NanotraceHR::Tracer tracer{"fetch document imports"_t, projectStorageCategory()};

    return s->selectAllDocumentImportForSourceIdStatement.valuesWithTransaction<Storage::Imports>();
}

void ProjectStorage::resetForTestsOnly()
{
    database.clearAllTablesForTestsOnly();
    commonTypeCache_.clearForTestsOnly();
    moduleCache.clearForTestOnly();
}

bool ProjectStorage::moduleNameLess(Utils::SmallStringView first, Utils::SmallStringView second) noexcept
{
    return first < second;
}

ModuleId ProjectStorage::fetchModuleId(Utils::SmallStringView moduleName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch module id"_t,
                               projectStorageCategory(),
                               keyValue("module name", moduleName)};

    auto moduleId = Sqlite::withDeferredTransaction(database, [&] {
        return fetchModuleIdUnguarded(moduleName);
    });

    tracer.end(keyValue("module id", moduleId));

    return moduleId;
}

Utils::PathString ProjectStorage::fetchModuleName(ModuleId id)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch module name"_t,
                               projectStorageCategory(),
                               keyValue("module id", id)};

    auto moduleName = Sqlite::withDeferredTransaction(database,
                                                      [&] { return fetchModuleNameUnguarded(id); });

    tracer.end(keyValue("module name", moduleName));

    return moduleName;
}

ProjectStorage::Modules ProjectStorage::fetchAllModules() const
{
    NanotraceHR::Tracer tracer{"fetch all modules"_t, projectStorageCategory()};

    return s->selectAllModulesStatement.valuesWithTransaction<Module, 128>();
}

void ProjectStorage::callRefreshMetaInfoCallback(const TypeIds &deletedTypeIds)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"call refresh meta info callback"_t,
                               projectStorageCategory(),
                               keyValue("type ids", deletedTypeIds)};

    if (deletedTypeIds.size()) {
        for (ProjectStorageObserver *observer : observers)
            observer->removedTypeIds(deletedTypeIds);
    }
}

SourceIds ProjectStorage::filterSourceIdsWithoutType(const SourceIds &updatedSourceIds,
                                                     SourceIds &sourceIdsOfTypes)
{
    std::sort(sourceIdsOfTypes.begin(), sourceIdsOfTypes.end());

    SourceIds sourceIdsWithoutTypeSourceIds;
    sourceIdsWithoutTypeSourceIds.reserve(updatedSourceIds.size());
    std::set_difference(updatedSourceIds.begin(),
                        updatedSourceIds.end(),
                        sourceIdsOfTypes.begin(),
                        sourceIdsOfTypes.end(),
                        std::back_inserter(sourceIdsWithoutTypeSourceIds));

    return sourceIdsWithoutTypeSourceIds;
}

TypeIds ProjectStorage::fetchTypeIds(const SourceIds &sourceIds)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch type ids"_t,
                               projectStorageCategory(),
                               keyValue("source ids", sourceIds)};

    return s->selectTypeIdsForSourceIdsStatement.values<TypeId, 128>(toIntegers(sourceIds));
}

void ProjectStorage::unique(SourceIds &sourceIds)
{
    std::sort(sourceIds.begin(), sourceIds.end());
    auto newEnd = std::unique(sourceIds.begin(), sourceIds.end());
    sourceIds.erase(newEnd, sourceIds.end());
}

void ProjectStorage::synchronizeTypeTraits(TypeId typeId, Storage::TypeTraits traits)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"synchronize type traits"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("type traits", traits)};

    s->updateTypeAnnotationTraitStatement.write(typeId, traits.annotation);
}

void ProjectStorage::updateTypeIdInTypeAnnotations(Storage::Synchronization::TypeAnnotations &typeAnnotations)
{
    NanotraceHR::Tracer tracer{"update type id in type annotations"_t, projectStorageCategory()};

    for (auto &annotation : typeAnnotations) {
        annotation.typeId = fetchTypeIdByModuleIdAndExportedName(annotation.moduleId,
                                                                 annotation.typeName);
    }

    for (auto &annotation : typeAnnotations) {
        if (!annotation.typeId)
            qWarning() << moduleName(annotation.moduleId).toQString()
                       << annotation.typeName.toQString();
    }

    typeAnnotations.erase(std::remove_if(typeAnnotations.begin(),
                                         typeAnnotations.end(),
                                         [](const auto &annotation) { return !annotation.typeId; }),
                          typeAnnotations.end());
}

void ProjectStorage::synchronizeTypeAnnotations(Storage::Synchronization::TypeAnnotations &typeAnnotations,
                                                const SourceIds &updatedTypeAnnotationSourceIds)
{
    NanotraceHR::Tracer tracer{"synchronize type annotations"_t, projectStorageCategory()};

    using Storage::Synchronization::TypeAnnotation;

    updateTypeIdInTypeAnnotations(typeAnnotations);

    auto compareKey = [](auto &&first, auto &&second) { return first.typeId - second.typeId; };

    std::sort(typeAnnotations.begin(), typeAnnotations.end(), [&](auto &&first, auto &&second) {
        return first.typeId < second.typeId;
    });

    auto range = s->selectTypeAnnotationsForSourceIdsStatement.range<TypeAnnotationView>(
        toIntegers(updatedTypeAnnotationSourceIds));

    auto insert = [&](const TypeAnnotation &annotation) {
        if (!annotation.sourceId)
            throw TypeAnnotationHasInvalidSourceId{};

        synchronizeTypeTraits(annotation.typeId, annotation.traits);

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert type annotations"_t,
                                   projectStorageCategory(),
                                   keyValue("type annotation", annotation)};

        s->insertTypeAnnotationStatement.write(annotation.typeId,
                                               annotation.sourceId,
                                               annotation.directorySourceId,
                                               annotation.iconPath,
                                               createEmptyAsNull(annotation.itemLibraryJson),
                                               createEmptyAsNull(annotation.hintsJson));
    };

    auto update = [&](const TypeAnnotationView &annotationFromDatabase,
                      const TypeAnnotation &annotation) {
        synchronizeTypeTraits(annotation.typeId, annotation.traits);

        if (annotationFromDatabase.iconPath != annotation.iconPath
            || annotationFromDatabase.itemLibraryJson != annotation.itemLibraryJson
            || annotationFromDatabase.hintsJson != annotation.hintsJson) {
            using NanotraceHR::keyValue;
            NanotraceHR::Tracer tracer{"update type annotations"_t,
                                       projectStorageCategory(),
                                       keyValue("type annotation from database",
                                                annotationFromDatabase),
                                       keyValue("type annotation", annotation)};

            s->updateTypeAnnotationStatement.write(annotation.typeId,
                                                   annotation.iconPath,
                                                   createEmptyAsNull(annotation.itemLibraryJson),
                                                   createEmptyAsNull(annotation.hintsJson));
            return Sqlite::UpdateChange::Update;
        }

        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const TypeAnnotationView &annotationFromDatabase) {
        synchronizeTypeTraits(annotationFromDatabase.typeId, Storage::TypeTraits{});

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove type annotations"_t,
                                   projectStorageCategory(),
                                   keyValue("type annotation", annotationFromDatabase)};

        s->deleteTypeAnnotationStatement.write(annotationFromDatabase.typeId);
    };

    Sqlite::insertUpdateDelete(range, typeAnnotations, compareKey, insert, update, remove);
}

void ProjectStorage::synchronizeTypeTrait(const Storage::Synchronization::Type &type)
{
    s->updateTypeTraitStatement.write(type.typeId, type.traits.type);
}

void ProjectStorage::synchronizeTypes(Storage::Synchronization::Types &types,
                                      TypeIds &updatedTypeIds,
                                      AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
                                      AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
                                      AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                                      PropertyDeclarations &relinkablePropertyDeclarations,
                                      Prototypes &relinkablePrototypes,
                                      Prototypes &relinkableExtensions,
                                      const SourceIds &updatedSourceIds)
{
    NanotraceHR::Tracer tracer{"synchronize types"_t, projectStorageCategory()};

    Storage::Synchronization::ExportedTypes exportedTypes;
    exportedTypes.reserve(types.size() * 3);
    SourceIds sourceIdsOfTypes;
    sourceIdsOfTypes.reserve(updatedSourceIds.size());
    SourceIds notUpdatedExportedSourceIds;
    notUpdatedExportedSourceIds.reserve(updatedSourceIds.size());
    SourceIds exportedSourceIds;
    exportedSourceIds.reserve(types.size());

    for (auto &type : types) {
        if (!type.sourceId)
            throw TypeHasInvalidSourceId{};

        TypeId typeId = declareType(type);
        synchronizeTypeTrait(type);
        sourceIdsOfTypes.push_back(type.sourceId);
        updatedTypeIds.push_back(typeId);
        if (type.changeLevel != Storage::Synchronization::ChangeLevel::ExcludeExportedTypes) {
            exportedSourceIds.push_back(type.sourceId);
            extractExportedTypes(typeId, type, exportedTypes);
        }
    }

    std::sort(types.begin(), types.end(), [](const auto &first, const auto &second) {
        return first.typeId < second.typeId;
    });

    unique(exportedSourceIds);

    SourceIds sourceIdsWithoutType = filterSourceIdsWithoutType(updatedSourceIds, sourceIdsOfTypes);
    exportedSourceIds.insert(exportedSourceIds.end(),
                             sourceIdsWithoutType.begin(),
                             sourceIdsWithoutType.end());
    TypeIds exportedTypeIds = fetchTypeIds(exportedSourceIds);
    synchronizeExportedTypes(exportedTypeIds,
                             exportedTypes,
                             relinkableAliasPropertyDeclarations,
                             relinkablePropertyDeclarations,
                             relinkablePrototypes,
                             relinkableExtensions);

    syncPrototypesAndExtensions(types, relinkablePrototypes, relinkableExtensions);
    resetDefaultPropertiesIfChanged(types);
    resetRemovedAliasPropertyDeclarationsToNull(types, relinkableAliasPropertyDeclarations);
    syncDeclarations(types,
                     insertedAliasPropertyDeclarations,
                     updatedAliasPropertyDeclarations,
                     relinkablePropertyDeclarations);
    syncDefaultProperties(types);
}

void ProjectStorage::synchronizeProjectDatas(Storage::Synchronization::ProjectDatas &projectDatas,
                                             const SourceIds &updatedProjectSourceIds)
{
    NanotraceHR::Tracer tracer{"synchronize project datas"_t, projectStorageCategory()};

    auto compareKey = [](auto &&first, auto &&second) {
        auto projectSourceIdDifference = first.projectSourceId - second.projectSourceId;
        if (projectSourceIdDifference != 0)
            return projectSourceIdDifference;

        return first.sourceId - second.sourceId;
    };

    std::sort(projectDatas.begin(), projectDatas.end(), [&](auto &&first, auto &&second) {
        return std::tie(first.projectSourceId, first.sourceId)
               < std::tie(second.projectSourceId, second.sourceId);
    });

    auto range = s->selectProjectDatasForSourceIdsStatement.range<Storage::Synchronization::ProjectData>(
        toIntegers(updatedProjectSourceIds));

    auto insert = [&](const Storage::Synchronization::ProjectData &projectData) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert project data"_t,
                                   projectStorageCategory(),
                                   keyValue("project data", projectData)};

        if (!projectData.projectSourceId)
            throw ProjectDataHasInvalidProjectSourceId{};
        if (!projectData.sourceId)
            throw ProjectDataHasInvalidSourceId{};

        s->insertProjectDataStatement.write(projectData.projectSourceId,
                                            projectData.sourceId,
                                            projectData.moduleId,
                                            projectData.fileType);
    };

    auto update = [&](const Storage::Synchronization::ProjectData &projectDataFromDatabase,
                      const Storage::Synchronization::ProjectData &projectData) {
        if (projectDataFromDatabase.fileType != projectData.fileType
            || !compareInvalidAreTrue(projectDataFromDatabase.moduleId, projectData.moduleId)) {
            using NanotraceHR::keyValue;
            NanotraceHR::Tracer tracer{"update project data"_t,
                                       projectStorageCategory(),
                                       keyValue("project data", projectData),
                                       keyValue("project data from database", projectDataFromDatabase)};

            s->updateProjectDataStatement.write(projectData.projectSourceId,
                                                projectData.sourceId,
                                                projectData.moduleId,
                                                projectData.fileType);
            return Sqlite::UpdateChange::Update;
        }

        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const Storage::Synchronization::ProjectData &projectData) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove project data"_t,
                                   projectStorageCategory(),
                                   keyValue("project data", projectData)};

        s->deleteProjectDataStatement.write(projectData.projectSourceId, projectData.sourceId);
    };

    Sqlite::insertUpdateDelete(range, projectDatas, compareKey, insert, update, remove);
}

void ProjectStorage::synchronizeFileStatuses(FileStatuses &fileStatuses,
                                             const SourceIds &updatedSourceIds)
{
    NanotraceHR::Tracer tracer{"synchronize file statuses"_t, projectStorageCategory()};

    auto compareKey = [](auto &&first, auto &&second) { return first.sourceId - second.sourceId; };

    std::sort(fileStatuses.begin(), fileStatuses.end(), [&](auto &&first, auto &&second) {
        return first.sourceId < second.sourceId;
    });

    auto range = s->selectFileStatusesForSourceIdsStatement.range<FileStatus>(
        toIntegers(updatedSourceIds));

    auto insert = [&](const FileStatus &fileStatus) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert file status"_t,
                                   projectStorageCategory(),
                                   keyValue("file status", fileStatus)};

        if (!fileStatus.sourceId)
            throw FileStatusHasInvalidSourceId{};
        s->insertFileStatusStatement.write(fileStatus.sourceId,
                                           fileStatus.size,
                                           fileStatus.lastModified);
    };

    auto update = [&](const FileStatus &fileStatusFromDatabase, const FileStatus &fileStatus) {
        if (fileStatusFromDatabase.lastModified != fileStatus.lastModified
            || fileStatusFromDatabase.size != fileStatus.size) {
            using NanotraceHR::keyValue;
            NanotraceHR::Tracer tracer{"update file status"_t,
                                       projectStorageCategory(),
                                       keyValue("file status", fileStatus),
                                       keyValue("file status from database", fileStatusFromDatabase)};

            s->updateFileStatusStatement.write(fileStatus.sourceId,
                                               fileStatus.size,
                                               fileStatus.lastModified);
            return Sqlite::UpdateChange::Update;
        }

        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const FileStatus &fileStatus) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove file status"_t,
                                   projectStorageCategory(),
                                   keyValue("file status", fileStatus)};

        s->deleteFileStatusStatement.write(fileStatus.sourceId);
    };

    Sqlite::insertUpdateDelete(range, fileStatuses, compareKey, insert, update, remove);
}

void ProjectStorage::synchronizeImports(Storage::Imports &imports,
                                        const SourceIds &updatedSourceIds,
                                        Storage::Imports &moduleDependencies,
                                        const SourceIds &updatedModuleDependencySourceIds,
                                        Storage::Synchronization::ModuleExportedImports &moduleExportedImports,
                                        const ModuleIds &updatedModuleIds)
{
    NanotraceHR::Tracer tracer{"synchronize imports"_t, projectStorageCategory()};

    synchromizeModuleExportedImports(moduleExportedImports, updatedModuleIds);
    NanotraceHR::Tracer importTracer{"synchronize qml document imports"_t, projectStorageCategory()};
    synchronizeDocumentImports(imports, updatedSourceIds, Storage::Synchronization::ImportKind::Import);
    importTracer.end();
    NanotraceHR::Tracer moduleDependenciesTracer{"synchronize module depdencies"_t,
                                                 projectStorageCategory()};
    synchronizeDocumentImports(moduleDependencies,
                               updatedModuleDependencySourceIds,
                               Storage::Synchronization::ImportKind::ModuleDependency);
    moduleDependenciesTracer.end();
}

void ProjectStorage::synchromizeModuleExportedImports(
    Storage::Synchronization::ModuleExportedImports &moduleExportedImports,
    const ModuleIds &updatedModuleIds)
{
    NanotraceHR::Tracer tracer{"synchronize module exported imports"_t, projectStorageCategory()};
    std::sort(moduleExportedImports.begin(),
              moduleExportedImports.end(),
              [](auto &&first, auto &&second) {
                  return std::tie(first.moduleId, first.exportedModuleId)
                         < std::tie(second.moduleId, second.exportedModuleId);
              });

    auto range = s->selectModuleExportedImportsForSourceIdStatement
                     .range<Storage::Synchronization::ModuleExportedImportView>(
                         toIntegers(updatedModuleIds));

    auto compareKey = [](const Storage::Synchronization::ModuleExportedImportView &view,
                         const Storage::Synchronization::ModuleExportedImport &import) -> long long {
        auto moduleIdDifference = view.moduleId - import.moduleId;
        if (moduleIdDifference != 0)
            return moduleIdDifference;

        return view.exportedModuleId - import.exportedModuleId;
    };

    auto insert = [&](const Storage::Synchronization::ModuleExportedImport &import) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert module exported import"_t,
                                   projectStorageCategory(),
                                   keyValue("module exported import", import),
                                   keyValue("module id", import.moduleId)};
        tracer.tick("exported module"_t, keyValue("module id", import.exportedModuleId));

        if (import.version.minor) {
            s->insertModuleExportedImportWithVersionStatement.write(import.moduleId,
                                                                    import.exportedModuleId,
                                                                    import.isAutoVersion,
                                                                    import.version.major.value,
                                                                    import.version.minor.value);
        } else if (import.version.major) {
            s->insertModuleExportedImportWithMajorVersionStatement.write(import.moduleId,
                                                                         import.exportedModuleId,
                                                                         import.isAutoVersion,
                                                                         import.version.major.value);
        } else {
            s->insertModuleExportedImportWithoutVersionStatement.write(import.moduleId,
                                                                       import.exportedModuleId,
                                                                       import.isAutoVersion);
        }
    };

    auto update = [](const Storage::Synchronization::ModuleExportedImportView &,
                     const Storage::Synchronization::ModuleExportedImport &) {
        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const Storage::Synchronization::ModuleExportedImportView &view) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove module exported import"_t,
                                   projectStorageCategory(),
                                   keyValue("module exported import view", view),
                                   keyValue("module id", view.moduleId)};
        tracer.tick("exported module"_t, keyValue("module id", view.exportedModuleId));

        s->deleteModuleExportedImportStatement.write(view.moduleExportedImportId);
    };

    Sqlite::insertUpdateDelete(range, moduleExportedImports, compareKey, insert, update, remove);
}

ModuleId ProjectStorage::fetchModuleIdUnguarded(Utils::SmallStringView name) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch module id ungarded"_t,
                               projectStorageCategory(),
                               keyValue("module name", name)};

    auto moduleId = s->selectModuleIdByNameStatement.value<ModuleId>(name);

    if (!moduleId)
        moduleId = s->insertModuleNameStatement.value<ModuleId>(name);

    tracer.end(keyValue("module id", moduleId));

    return moduleId;
}

Utils::PathString ProjectStorage::fetchModuleNameUnguarded(ModuleId id) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch module name ungarded"_t,
                               projectStorageCategory(),
                               keyValue("module id", id)};

    auto moduleName = s->selectModuleNameStatement.value<Utils::PathString>(id);

    if (moduleName.empty())
        throw ModuleDoesNotExists{};

    tracer.end(keyValue("module name", moduleName));

    return moduleName;
}

void ProjectStorage::handleAliasPropertyDeclarationsWithPropertyType(
    TypeId typeId, AliasPropertyDeclarations &relinkableAliasPropertyDeclarations)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"handle alias property declarations with property type"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("relinkable alias property declarations",
                                        relinkableAliasPropertyDeclarations)};

    auto callback = [&](TypeId typeId_,
                        PropertyDeclarationId propertyDeclarationId,
                        ImportedTypeNameId propertyImportedTypeNameId,
                        PropertyDeclarationId aliasPropertyDeclarationId,
                        PropertyDeclarationId aliasPropertyDeclarationTailId) {
        auto aliasPropertyName = s->selectPropertyNameStatement.value<Utils::SmallString>(
            aliasPropertyDeclarationId);
        Utils::SmallString aliasPropertyNameTail;
        if (aliasPropertyDeclarationTailId)
            aliasPropertyNameTail = s->selectPropertyNameStatement.value<Utils::SmallString>(
                aliasPropertyDeclarationTailId);

        relinkableAliasPropertyDeclarations.emplace_back(TypeId{typeId_},
                                                         PropertyDeclarationId{propertyDeclarationId},
                                                         ImportedTypeNameId{propertyImportedTypeNameId},
                                                         std::move(aliasPropertyName),
                                                         std::move(aliasPropertyNameTail));

        s->updateAliasPropertyDeclarationToNullStatement.write(propertyDeclarationId);
    };

    s->selectAliasPropertiesDeclarationForPropertiesWithTypeIdStatement.readCallback(callback, typeId);
}

void ProjectStorage::handlePropertyDeclarationWithPropertyType(
    TypeId typeId, PropertyDeclarations &relinkablePropertyDeclarations)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"handle property declarations with property type"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("relinkable property declarations",
                                        relinkablePropertyDeclarations)};

    s->updatesPropertyDeclarationPropertyTypeToNullStatement.readTo(relinkablePropertyDeclarations,
                                                                    typeId);
}

void ProjectStorage::handlePrototypes(TypeId prototypeId, Prototypes &relinkablePrototypes)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"handle prototypes"_t,
                               projectStorageCategory(),
                               keyValue("type id", prototypeId),
                               keyValue("relinkable prototypes", relinkablePrototypes)};

    auto callback = [&](TypeId typeId, ImportedTypeNameId prototypeNameId) {
        relinkablePrototypes.emplace_back(typeId, prototypeNameId);
    };

    s->updatePrototypeIdToNullStatement.readCallback(callback, prototypeId);
}

void ProjectStorage::handleExtensions(TypeId extensionId, Prototypes &relinkableExtensions)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"handle extension"_t,
                               projectStorageCategory(),
                               keyValue("type id", extensionId),
                               keyValue("relinkable extensions", relinkableExtensions)};

    auto callback = [&](TypeId typeId, ImportedTypeNameId extensionNameId) {
        relinkableExtensions.emplace_back(typeId, extensionNameId);
    };

    s->updateExtensionIdToNullStatement.readCallback(callback, extensionId);
}

void ProjectStorage::deleteType(TypeId typeId,
                                AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                                PropertyDeclarations &relinkablePropertyDeclarations,
                                Prototypes &relinkablePrototypes,
                                Prototypes &relinkableExtensions)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"delete type"_t, projectStorageCategory(), keyValue("type id", typeId)};

    handlePropertyDeclarationWithPropertyType(typeId, relinkablePropertyDeclarations);
    handleAliasPropertyDeclarationsWithPropertyType(typeId, relinkableAliasPropertyDeclarations);
    handlePrototypes(typeId, relinkablePrototypes);
    handleExtensions(typeId, relinkableExtensions);
    s->deleteTypeNamesByTypeIdStatement.write(typeId);
    s->deleteEnumerationDeclarationByTypeIdStatement.write(typeId);
    s->deletePropertyDeclarationByTypeIdStatement.write(typeId);
    s->deleteFunctionDeclarationByTypeIdStatement.write(typeId);
    s->deleteSignalDeclarationByTypeIdStatement.write(typeId);
    s->deleteTypeStatement.write(typeId);
}

void ProjectStorage::relinkAliasPropertyDeclarations(AliasPropertyDeclarations &aliasPropertyDeclarations,
                                                     const TypeIds &deletedTypeIds)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"relink alias properties"_t,
                               projectStorageCategory(),
                               keyValue("alias property declarations", aliasPropertyDeclarations),
                               keyValue("deleted type ids", deletedTypeIds)};

    std::sort(aliasPropertyDeclarations.begin(), aliasPropertyDeclarations.end());

    Utils::set_greedy_difference(
        aliasPropertyDeclarations.cbegin(),
        aliasPropertyDeclarations.cend(),
        deletedTypeIds.begin(),
        deletedTypeIds.end(),
        [&](const AliasPropertyDeclaration &alias) {
            auto typeId = fetchTypeId(alias.aliasImportedTypeNameId);

            if (!typeId)
                throw TypeNameDoesNotExists{fetchImportedTypeName(alias.aliasImportedTypeNameId)};

            auto [propertyTypeId, aliasId, propertyTraits] = fetchPropertyDeclarationByTypeIdAndNameUngarded(
                typeId, alias.aliasPropertyName);

            s->updatePropertyDeclarationWithAliasAndTypeStatement.write(alias.propertyDeclarationId,
                                                                        propertyTypeId,
                                                                        propertyTraits,
                                                                        alias.aliasImportedTypeNameId,
                                                                        aliasId);
        },
        TypeCompare<AliasPropertyDeclaration>{});
}

void ProjectStorage::relinkPropertyDeclarations(PropertyDeclarations &relinkablePropertyDeclaration,
                                                const TypeIds &deletedTypeIds)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"relink property declarations"_t,
                               projectStorageCategory(),
                               keyValue("relinkable property declarations",
                                        relinkablePropertyDeclaration),
                               keyValue("deleted type ids", deletedTypeIds)};

    std::sort(relinkablePropertyDeclaration.begin(), relinkablePropertyDeclaration.end());

    Utils::set_greedy_difference(
        relinkablePropertyDeclaration.cbegin(),
        relinkablePropertyDeclaration.cend(),
        deletedTypeIds.begin(),
        deletedTypeIds.end(),
        [&](const PropertyDeclaration &property) {
            TypeId propertyTypeId = fetchTypeId(property.importedTypeNameId);

            if (!propertyTypeId)
                throw TypeNameDoesNotExists{fetchImportedTypeName(property.importedTypeNameId)};

            s->updatePropertyDeclarationTypeStatement.write(property.propertyDeclarationId,
                                                            propertyTypeId);
        },
        TypeCompare<PropertyDeclaration>{});
}

void ProjectStorage::deleteNotUpdatedTypes(const TypeIds &updatedTypeIds,
                                           const SourceIds &updatedSourceIds,
                                           const TypeIds &typeIdsToBeDeleted,
                                           AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                                           PropertyDeclarations &relinkablePropertyDeclarations,
                                           Prototypes &relinkablePrototypes,
                                           Prototypes &relinkableExtensions,
                                           TypeIds &deletedTypeIds)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"delete not updated types"_t,
                               projectStorageCategory(),
                               keyValue("updated type ids", updatedTypeIds),
                               keyValue("updated source ids", updatedSourceIds),
                               keyValue("type ids to be deleted", typeIdsToBeDeleted)};

    auto callback = [&](TypeId typeId) {
        deletedTypeIds.push_back(typeId);
        deleteType(typeId,
                   relinkableAliasPropertyDeclarations,
                   relinkablePropertyDeclarations,
                   relinkablePrototypes,
                   relinkableExtensions);
    };

    s->selectNotUpdatedTypesInSourcesStatement.readCallback(callback,
                                                            toIntegers(updatedSourceIds),
                                                            toIntegers(updatedTypeIds));
    for (TypeId typeIdToBeDeleted : typeIdsToBeDeleted)
        callback(typeIdToBeDeleted);
}

void ProjectStorage::relink(AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                            PropertyDeclarations &relinkablePropertyDeclarations,
                            Prototypes &relinkablePrototypes,
                            Prototypes &relinkableExtensions,
                            TypeIds &deletedTypeIds)
{
    NanotraceHR::Tracer tracer{"relink"_t, projectStorageCategory()};

    std::sort(deletedTypeIds.begin(), deletedTypeIds.end());

    relinkPrototypes(relinkablePrototypes, deletedTypeIds, [&](TypeId typeId, TypeId prototypeId) {
        s->updateTypePrototypeStatement.write(typeId, prototypeId);
    });
    relinkPrototypes(relinkableExtensions, deletedTypeIds, [&](TypeId typeId, TypeId prototypeId) {
        s->updateTypeExtensionStatement.write(typeId, prototypeId);
    });
    relinkPropertyDeclarations(relinkablePropertyDeclarations, deletedTypeIds);
    relinkAliasPropertyDeclarations(relinkableAliasPropertyDeclarations, deletedTypeIds);
}

PropertyDeclarationId ProjectStorage::fetchAliasId(TypeId aliasTypeId,
                                                   Utils::SmallStringView aliasPropertyName,
                                                   Utils::SmallStringView aliasPropertyNameTail)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch alias id"_t,
                               projectStorageCategory(),
                               keyValue("alias type id", aliasTypeId),
                               keyValue("alias property name", aliasPropertyName),
                               keyValue("alias property name tail", aliasPropertyNameTail)};

    if (aliasPropertyNameTail.empty())
        return fetchPropertyDeclarationIdByTypeIdAndNameUngarded(aliasTypeId, aliasPropertyName);

    auto stemAlias = fetchPropertyDeclarationByTypeIdAndNameUngarded(aliasTypeId, aliasPropertyName);

    return fetchPropertyDeclarationIdByTypeIdAndNameUngarded(stemAlias.propertyTypeId,
                                                             aliasPropertyNameTail);
}

void ProjectStorage::linkAliasPropertyDeclarationAliasIds(const AliasPropertyDeclarations &aliasDeclarations)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"link alias property declarations alias ids"_t,
                               projectStorageCategory(),
                               keyValue("alias property declarations", aliasDeclarations)};

    for (const auto &aliasDeclaration : aliasDeclarations) {
        auto aliasTypeId = fetchTypeId(aliasDeclaration.aliasImportedTypeNameId);

        if (!aliasTypeId) {
            throw TypeNameDoesNotExists{
                fetchImportedTypeName(aliasDeclaration.aliasImportedTypeNameId)};
        }

        auto aliasId = fetchAliasId(aliasTypeId,
                                    aliasDeclaration.aliasPropertyName,
                                    aliasDeclaration.aliasPropertyNameTail);

        s->updatePropertyDeclarationAliasIdAndTypeNameIdStatement.write(
            aliasDeclaration.propertyDeclarationId, aliasId, aliasDeclaration.aliasImportedTypeNameId);
    }
}

void ProjectStorage::updateAliasPropertyDeclarationValues(const AliasPropertyDeclarations &aliasDeclarations)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"update alias property declarations"_t,
                               projectStorageCategory(),
                               keyValue("alias property declarations", aliasDeclarations)};

    for (const auto &aliasDeclaration : aliasDeclarations) {
        s->updatetPropertiesDeclarationValuesOfAliasStatement.write(
            aliasDeclaration.propertyDeclarationId);
        s->updatePropertyAliasDeclarationRecursivelyStatement.write(
            aliasDeclaration.propertyDeclarationId);
    }
}

void ProjectStorage::checkAliasPropertyDeclarationCycles(const AliasPropertyDeclarations &aliasDeclarations)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"check alias property declarations cycles"_t,
                               projectStorageCategory(),
                               keyValue("alias property declarations", aliasDeclarations)};
    for (const auto &aliasDeclaration : aliasDeclarations)
        checkForAliasChainCycle(aliasDeclaration.propertyDeclarationId);
}

void ProjectStorage::linkAliases(const AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
                                 const AliasPropertyDeclarations &updatedAliasPropertyDeclarations)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"link aliases"_t, projectStorageCategory()};

    linkAliasPropertyDeclarationAliasIds(insertedAliasPropertyDeclarations);
    linkAliasPropertyDeclarationAliasIds(updatedAliasPropertyDeclarations);

    checkAliasPropertyDeclarationCycles(insertedAliasPropertyDeclarations);
    checkAliasPropertyDeclarationCycles(updatedAliasPropertyDeclarations);

    updateAliasPropertyDeclarationValues(insertedAliasPropertyDeclarations);
    updateAliasPropertyDeclarationValues(updatedAliasPropertyDeclarations);
}

void ProjectStorage::synchronizeExportedTypes(const TypeIds &updatedTypeIds,
                                              Storage::Synchronization::ExportedTypes &exportedTypes,
                                              AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                                              PropertyDeclarations &relinkablePropertyDeclarations,
                                              Prototypes &relinkablePrototypes,
                                              Prototypes &relinkableExtensions)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"synchronize exported types"_t, projectStorageCategory()};

    std::sort(exportedTypes.begin(), exportedTypes.end(), [](auto &&first, auto &&second) {
        if (first.moduleId < second.moduleId)
            return true;
        else if (first.moduleId > second.moduleId)
            return false;

        auto nameCompare = Sqlite::compare(first.name, second.name);

        if (nameCompare < 0)
            return true;
        else if (nameCompare > 0)
            return false;

        return first.version < second.version;
    });

    auto range = s->selectExportedTypesForSourceIdsStatement
                     .range<Storage::Synchronization::ExportedTypeView>(toIntegers(updatedTypeIds));

    auto compareKey = [](const Storage::Synchronization::ExportedTypeView &view,
                         const Storage::Synchronization::ExportedType &type) -> long long {
        auto moduleIdDifference = view.moduleId - type.moduleId;
        if (moduleIdDifference != 0)
            return moduleIdDifference;

        auto nameDifference = Sqlite::compare(view.name, type.name);
        if (nameDifference != 0)
            return nameDifference;

        auto versionDifference = view.version.major.value - type.version.major.value;
        if (versionDifference != 0)
            return versionDifference;

        return view.version.minor.value - type.version.minor.value;
    };

    auto insert = [&](const Storage::Synchronization::ExportedType &type) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert exported type"_t,
                                   projectStorageCategory(),
                                   keyValue("exported type", type),
                                   keyValue("type id", type.typeId),
                                   keyValue("module id", type.moduleId)};
        if (!type.moduleId)
            throw QmlDesigner::ModuleDoesNotExists{};

        try {
            if (type.version) {
                s->insertExportedTypeNamesWithVersionStatement.write(type.moduleId,
                                                                     type.name,
                                                                     type.version.major.value,
                                                                     type.version.minor.value,
                                                                     type.typeId);

            } else if (type.version.major) {
                s->insertExportedTypeNamesWithMajorVersionStatement.write(type.moduleId,
                                                                          type.name,
                                                                          type.version.major.value,
                                                                          type.typeId);
            } else {
                s->insertExportedTypeNamesWithoutVersionStatement.write(type.moduleId,
                                                                        type.name,
                                                                        type.typeId);
            }
        } catch (const Sqlite::ConstraintPreventsModification &) {
            throw QmlDesigner::ExportedTypeCannotBeInserted{type.name};
        }
    };

    auto update = [&](const Storage::Synchronization::ExportedTypeView &view,
                      const Storage::Synchronization::ExportedType &type) {
        if (view.typeId != type.typeId) {
            NanotraceHR::Tracer tracer{"update exported type"_t,
                                       projectStorageCategory(),
                                       keyValue("exported type", type),
                                       keyValue("exported type view", view),
                                       keyValue("type id", type.typeId),
                                       keyValue("module id", type.typeId)};

            handlePropertyDeclarationWithPropertyType(view.typeId, relinkablePropertyDeclarations);
            handleAliasPropertyDeclarationsWithPropertyType(view.typeId,
                                                            relinkableAliasPropertyDeclarations);
            handlePrototypes(view.typeId, relinkablePrototypes);
            handleExtensions(view.typeId, relinkableExtensions);
            s->updateExportedTypeNameTypeIdStatement.write(view.exportedTypeNameId, type.typeId);
            return Sqlite::UpdateChange::Update;
        }
        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const Storage::Synchronization::ExportedTypeView &view) {
        NanotraceHR::Tracer tracer{"remove exported type"_t,
                                   projectStorageCategory(),
                                   keyValue("exported type", view),
                                   keyValue("type id", view.typeId),
                                   keyValue("module id", view.moduleId)};

        handlePropertyDeclarationWithPropertyType(view.typeId, relinkablePropertyDeclarations);
        handleAliasPropertyDeclarationsWithPropertyType(view.typeId,
                                                        relinkableAliasPropertyDeclarations);
        handlePrototypes(view.typeId, relinkablePrototypes);
        handleExtensions(view.typeId, relinkableExtensions);
        s->deleteExportedTypeNameStatement.write(view.exportedTypeNameId);
    };

    Sqlite::insertUpdateDelete(range, exportedTypes, compareKey, insert, update, remove);
}

void ProjectStorage::synchronizePropertyDeclarationsInsertAlias(
    AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
    const Storage::Synchronization::PropertyDeclaration &value,
    SourceId sourceId,
    TypeId typeId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"insert property declaration to alias"_t,
                               projectStorageCategory(),
                               keyValue("property declaration", value)};

    auto callback = [&](PropertyDeclarationId propertyDeclarationId) {
        insertedAliasPropertyDeclarations.emplace_back(typeId,
                                                       propertyDeclarationId,
                                                       fetchImportedTypeNameId(value.typeName,
                                                                               sourceId),
                                                       value.aliasPropertyName,
                                                       value.aliasPropertyNameTail);
        return Sqlite::CallbackControl::Abort;
    };

    s->insertAliasPropertyDeclarationStatement.readCallback(callback, typeId, value.name);
}

QVarLengthArray<PropertyDeclarationId, 128> ProjectStorage::fetchPropertyDeclarationIds(
    TypeId baseTypeId) const
{
    QVarLengthArray<PropertyDeclarationId, 128> propertyDeclarationIds;

    s->selectLocalPropertyDeclarationIdsForTypeStatement.readTo(propertyDeclarationIds, baseTypeId);

    auto range = s->selectPrototypeAndExtensionIdsStatement.range<TypeId>(baseTypeId);

    for (TypeId prototype : range) {
        s->selectLocalPropertyDeclarationIdsForTypeStatement.readTo(propertyDeclarationIds, prototype);
    }

    return propertyDeclarationIds;
}

PropertyDeclarationId ProjectStorage::fetchNextPropertyDeclarationId(
    TypeId baseTypeId, Utils::SmallStringView propertyName) const
{
    auto range = s->selectPrototypeAndExtensionIdsStatement.range<TypeId>(baseTypeId);

    for (TypeId prototype : range) {
        auto propertyDeclarationId = s->selectPropertyDeclarationIdByTypeIdAndNameStatement
                                         .value<PropertyDeclarationId>(prototype, propertyName);

        if (propertyDeclarationId)
            return propertyDeclarationId;
    }

    return PropertyDeclarationId{};
}

PropertyDeclarationId ProjectStorage::fetchPropertyDeclarationId(TypeId typeId,
                                                                 Utils::SmallStringView propertyName) const
{
    auto propertyDeclarationId = s->selectPropertyDeclarationIdByTypeIdAndNameStatement
                                     .value<PropertyDeclarationId>(typeId, propertyName);

    if (propertyDeclarationId)
        return propertyDeclarationId;

    return fetchNextPropertyDeclarationId(typeId, propertyName);
}

PropertyDeclarationId ProjectStorage::fetchNextDefaultPropertyDeclarationId(TypeId baseTypeId) const
{
    auto range = s->selectPrototypeAndExtensionIdsStatement.range<TypeId>(baseTypeId);

    for (TypeId prototype : range) {
        auto propertyDeclarationId = s->selectDefaultPropertyDeclarationIdStatement
                                         .value<PropertyDeclarationId>(prototype);

        if (propertyDeclarationId)
            return propertyDeclarationId;
    }

    return PropertyDeclarationId{};
}

PropertyDeclarationId ProjectStorage::fetchDefaultPropertyDeclarationId(TypeId typeId) const
{
    auto propertyDeclarationId = s->selectDefaultPropertyDeclarationIdStatement
                                     .value<PropertyDeclarationId>(typeId);

    if (propertyDeclarationId)
        return propertyDeclarationId;

    return fetchNextDefaultPropertyDeclarationId(typeId);
}

void ProjectStorage::synchronizePropertyDeclarationsInsertProperty(
    const Storage::Synchronization::PropertyDeclaration &value, SourceId sourceId, TypeId typeId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"insert property declaration"_t,
                               projectStorageCategory(),
                               keyValue("property declaration", value)};

    auto propertyImportedTypeNameId = fetchImportedTypeNameId(value.typeName, sourceId);
    auto propertyTypeId = fetchTypeId(propertyImportedTypeNameId);

    if (!propertyTypeId)
        throw TypeNameDoesNotExists{fetchImportedTypeName(propertyImportedTypeNameId), sourceId};

    auto propertyDeclarationId = s->insertPropertyDeclarationStatement.value<PropertyDeclarationId>(
        typeId, value.name, propertyTypeId, value.traits, propertyImportedTypeNameId);

    auto nextPropertyDeclarationId = fetchNextPropertyDeclarationId(typeId, value.name);
    if (nextPropertyDeclarationId) {
        s->updateAliasIdPropertyDeclarationStatement.write(nextPropertyDeclarationId,
                                                           propertyDeclarationId);
        s->updatePropertyAliasDeclarationRecursivelyWithTypeAndTraitsStatement
            .write(propertyDeclarationId, propertyTypeId, value.traits);
    }
}

void ProjectStorage::synchronizePropertyDeclarationsUpdateAlias(
    AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
    const Storage::Synchronization::PropertyDeclarationView &view,
    const Storage::Synchronization::PropertyDeclaration &value,
    SourceId sourceId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"update property declaration to alias"_t,
                               projectStorageCategory(),
                               keyValue("property declaration", value),
                               keyValue("property declaration view", view)};

    updatedAliasPropertyDeclarations.emplace_back(view.typeId,
                                                  view.id,
                                                  fetchImportedTypeNameId(value.typeName, sourceId),
                                                  value.aliasPropertyName,
                                                  value.aliasPropertyNameTail,
                                                  view.aliasId);
}

Sqlite::UpdateChange ProjectStorage::synchronizePropertyDeclarationsUpdateProperty(
    const Storage::Synchronization::PropertyDeclarationView &view,
    const Storage::Synchronization::PropertyDeclaration &value,
    SourceId sourceId,
    PropertyDeclarationIds &propertyDeclarationIds)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"update property declaration"_t,
                               projectStorageCategory(),
                               keyValue("property declaration", value),
                               keyValue("property declaration view", view)};

    auto propertyImportedTypeNameId = fetchImportedTypeNameId(value.typeName, sourceId);

    auto propertyTypeId = fetchTypeId(propertyImportedTypeNameId);

    if (!propertyTypeId)
        throw TypeNameDoesNotExists{fetchImportedTypeName(propertyImportedTypeNameId), sourceId};

    if (view.traits == value.traits && propertyTypeId == view.typeId
        && propertyImportedTypeNameId == view.typeNameId)
        return Sqlite::UpdateChange::No;

    s->updatePropertyDeclarationStatement.write(view.id,
                                                propertyTypeId,
                                                value.traits,
                                                propertyImportedTypeNameId);
    s->updatePropertyAliasDeclarationRecursivelyWithTypeAndTraitsStatement.write(view.id,
                                                                                 propertyTypeId,
                                                                                 value.traits);
    propertyDeclarationIds.push_back(view.id);

    tracer.end(keyValue("updated", "yes"));

    return Sqlite::UpdateChange::Update;
}

void ProjectStorage::synchronizePropertyDeclarations(
    TypeId typeId,
    Storage::Synchronization::PropertyDeclarations &propertyDeclarations,
    SourceId sourceId,
    AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
    AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
    PropertyDeclarationIds &propertyDeclarationIds)
{
    NanotraceHR::Tracer tracer{"synchronize property declaration"_t, projectStorageCategory()};

    std::sort(propertyDeclarations.begin(), propertyDeclarations.end(), [](auto &&first, auto &&second) {
        return Sqlite::compare(first.name, second.name) < 0;
    });

    auto range = s->selectPropertyDeclarationsForTypeIdStatement
                     .range<Storage::Synchronization::PropertyDeclarationView>(typeId);

    auto compareKey = [](const Storage::Synchronization::PropertyDeclarationView &view,
                         const Storage::Synchronization::PropertyDeclaration &value) {
        return Sqlite::compare(view.name, value.name);
    };

    auto insert = [&](const Storage::Synchronization::PropertyDeclaration &value) {
        if (value.kind == Storage::Synchronization::PropertyKind::Alias) {
            synchronizePropertyDeclarationsInsertAlias(insertedAliasPropertyDeclarations,
                                                       value,
                                                       sourceId,
                                                       typeId);
        } else {
            synchronizePropertyDeclarationsInsertProperty(value, sourceId, typeId);
        }
    };

    auto update = [&](const Storage::Synchronization::PropertyDeclarationView &view,
                      const Storage::Synchronization::PropertyDeclaration &value) {
        if (value.kind == Storage::Synchronization::PropertyKind::Alias) {
            synchronizePropertyDeclarationsUpdateAlias(updatedAliasPropertyDeclarations,
                                                       view,
                                                       value,
                                                       sourceId);
            propertyDeclarationIds.push_back(view.id);
        } else {
            return synchronizePropertyDeclarationsUpdateProperty(view,
                                                                 value,
                                                                 sourceId,
                                                                 propertyDeclarationIds);
        }

        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const Storage::Synchronization::PropertyDeclarationView &view) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove property declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("property declaratio viewn", view)};

        auto nextPropertyDeclarationId = fetchNextPropertyDeclarationId(typeId, view.name);

        if (nextPropertyDeclarationId) {
            s->updateAliasPropertyDeclarationByAliasPropertyDeclarationIdStatement
                .write(nextPropertyDeclarationId, view.id);
        }

        s->updateDefaultPropertyIdToNullStatement.write(view.id);
        s->deletePropertyDeclarationStatement.write(view.id);
        propertyDeclarationIds.push_back(view.id);
    };

    Sqlite::insertUpdateDelete(range, propertyDeclarations, compareKey, insert, update, remove);
}

void ProjectStorage::resetRemovedAliasPropertyDeclarationsToNull(
    Storage::Synchronization::Type &type, PropertyDeclarationIds &propertyDeclarationIds)
{
    NanotraceHR::Tracer tracer{"reset removed alias property declaration to null"_t,
                               projectStorageCategory()};

    if (type.changeLevel == Storage::Synchronization::ChangeLevel::Minimal)
        return;

    Storage::Synchronization::PropertyDeclarations &aliasDeclarations = type.propertyDeclarations;

    std::sort(aliasDeclarations.begin(), aliasDeclarations.end(), [](auto &&first, auto &&second) {
        return Sqlite::compare(first.name, second.name) < 0;
    });

    auto range = s->selectPropertyDeclarationsWithAliasForTypeIdStatement
                     .range<AliasPropertyDeclarationView>(type.typeId);

    auto compareKey = [](const AliasPropertyDeclarationView &view,
                         const Storage::Synchronization::PropertyDeclaration &value) {
        return Sqlite::compare(view.name, value.name);
    };

    auto insert = [&](const Storage::Synchronization::PropertyDeclaration &) {};

    auto update = [&](const AliasPropertyDeclarationView &,
                      const Storage::Synchronization::PropertyDeclaration &) {
        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const AliasPropertyDeclarationView &view) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"reset removed alias property declaration to null"_t,
                                   projectStorageCategory(),
                                   keyValue("alias property declaration view", view)};

        s->updatePropertyDeclarationAliasIdToNullStatement.write(view.id);
        propertyDeclarationIds.push_back(view.id);
    };

    Sqlite::insertUpdateDelete(range, aliasDeclarations, compareKey, insert, update, remove);
}

void ProjectStorage::resetRemovedAliasPropertyDeclarationsToNull(
    Storage::Synchronization::Types &types,
    AliasPropertyDeclarations &relinkableAliasPropertyDeclarations)
{
    NanotraceHR::Tracer tracer{"reset removed alias properties to null"_t, projectStorageCategory()};

    PropertyDeclarationIds propertyDeclarationIds;
    propertyDeclarationIds.reserve(types.size());

    for (auto &&type : types)
        resetRemovedAliasPropertyDeclarationsToNull(type, propertyDeclarationIds);

    removeRelinkableEntries(relinkableAliasPropertyDeclarations,
                            propertyDeclarationIds,
                            PropertyCompare<AliasPropertyDeclaration>{});
}

ImportId ProjectStorage::insertDocumentImport(const Storage::Import &import,
                                              Storage::Synchronization::ImportKind importKind,
                                              ModuleId sourceModuleId,
                                              ImportId parentImportId)
{
    if (import.version.minor) {
        return s->insertDocumentImportWithVersionStatement.value<ImportId>(import.sourceId,
                                                                           import.moduleId,
                                                                           sourceModuleId,
                                                                           importKind,
                                                                           import.version.major.value,
                                                                           import.version.minor.value,
                                                                           parentImportId);
    } else if (import.version.major) {
        return s->insertDocumentImportWithMajorVersionStatement.value<ImportId>(import.sourceId,
                                                                                import.moduleId,
                                                                                sourceModuleId,
                                                                                importKind,
                                                                                import.version.major.value,
                                                                                parentImportId);
    } else {
        return s->insertDocumentImportWithoutVersionStatement.value<ImportId>(import.sourceId,
                                                                              import.moduleId,
                                                                              sourceModuleId,
                                                                              importKind,
                                                                              parentImportId);
    }
}

void ProjectStorage::synchronizeDocumentImports(Storage::Imports &imports,
                                                const SourceIds &updatedSourceIds,
                                                Storage::Synchronization::ImportKind importKind)
{
    std::sort(imports.begin(), imports.end(), [](auto &&first, auto &&second) {
        return std::tie(first.sourceId, first.moduleId, first.version)
               < std::tie(second.sourceId, second.moduleId, second.version);
    });

    auto range = s->selectDocumentImportForSourceIdStatement
                     .range<Storage::Synchronization::ImportView>(toIntegers(updatedSourceIds),
                                                                  importKind);

    auto compareKey = [](const Storage::Synchronization::ImportView &view,
                         const Storage::Import &import) -> long long {
        auto sourceIdDifference = view.sourceId - import.sourceId;
        if (sourceIdDifference != 0)
            return sourceIdDifference;

        auto moduleIdDifference = view.moduleId - import.moduleId;
        if (moduleIdDifference != 0)
            return moduleIdDifference;

        auto versionDifference = view.version.major.value - import.version.major.value;
        if (versionDifference != 0)
            return versionDifference;

        return view.version.minor.value - import.version.minor.value;
    };

    auto insert = [&](const Storage::Import &import) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert import"_t,
                                   projectStorageCategory(),
                                   keyValue("import", import),
                                   keyValue("import kind", importKind),
                                   keyValue("source id", import.sourceId),
                                   keyValue("module id", import.moduleId)};

        auto importId = insertDocumentImport(import, importKind, import.moduleId, ImportId{});
        auto callback = [&](ModuleId exportedModuleId, int majorVersion, int minorVersion) {
            Storage::Import additionImport{exportedModuleId,
                                           Storage::Version{majorVersion, minorVersion},
                                           import.sourceId};

            auto exportedImportKind = importKind == Storage::Synchronization::ImportKind::Import
                                          ? Storage::Synchronization::ImportKind::ModuleExportedImport
                                          : Storage::Synchronization::ImportKind::ModuleExportedModuleDependency;

            NanotraceHR::Tracer tracer{"insert indirect import"_t,
                                       projectStorageCategory(),
                                       keyValue("import", import),
                                       keyValue("import kind", exportedImportKind),
                                       keyValue("source id", import.sourceId),
                                       keyValue("module id", import.moduleId)};

            auto indirectImportId = insertDocumentImport(additionImport,
                                                         exportedImportKind,
                                                         import.moduleId,
                                                         importId);

            tracer.end(keyValue("import id", indirectImportId));
        };

        s->selectModuleExportedImportsForModuleIdStatement.readCallback(callback,
                                                                        import.moduleId,
                                                                        import.version.major.value,
                                                                        import.version.minor.value);
        tracer.end(keyValue("import id", importId));
    };

    auto update = [](const Storage::Synchronization::ImportView &, const Storage::Import &) {
        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const Storage::Synchronization::ImportView &view) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove import"_t,
                                   projectStorageCategory(),
                                   keyValue("import", view),
                                   keyValue("import id", view.importId),
                                   keyValue("source id", view.sourceId),
                                   keyValue("module id", view.moduleId)};

        s->deleteDocumentImportStatement.write(view.importId);
        s->deleteDocumentImportsWithParentImportIdStatement.write(view.sourceId, view.importId);
    };

    Sqlite::insertUpdateDelete(range, imports, compareKey, insert, update, remove);
}

Utils::PathString ProjectStorage::createJson(const Storage::Synchronization::ParameterDeclarations &parameters)
{
    NanotraceHR::Tracer tracer{"create json from parameter declarations"_t, projectStorageCategory()};

    Utils::PathString json;
    json.append("[");

    Utils::SmallStringView comma{""};

    for (const auto &parameter : parameters) {
        json.append(comma);
        comma = ",";
        json.append(R"({"n":")");
        json.append(parameter.name);
        json.append(R"(","tn":")");
        json.append(parameter.typeName);
        if (parameter.traits == Storage::PropertyDeclarationTraits::None) {
            json.append("\"}");
        } else {
            json.append(R"(","tr":)");
            json.append(Utils::SmallString::number(to_underlying(parameter.traits)));
            json.append("}");
        }
    }

    json.append("]");

    return json;
}

TypeId ProjectStorage::fetchTypeIdByModuleIdAndExportedName(ModuleId moduleId,
                                                            Utils::SmallStringView name) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch type id by module id and exported name"_t,
                               projectStorageCategory(),
                               keyValue("module id", moduleId),
                               keyValue("exported name", name)};

    return s->selectTypeIdByModuleIdAndExportedNameStatement.value<TypeId>(moduleId, name);
}

void ProjectStorage::addTypeIdToPropertyEditorQmlPaths(
    Storage::Synchronization::PropertyEditorQmlPaths &paths)
{
    NanotraceHR::Tracer tracer{"add type id to property editor qml paths"_t, projectStorageCategory()};

    for (auto &path : paths)
        path.typeId = fetchTypeIdByModuleIdAndExportedName(path.moduleId, path.typeName);
}

void ProjectStorage::synchronizePropertyEditorPaths(Storage::Synchronization::PropertyEditorQmlPaths &paths,
                                                    SourceIds updatedPropertyEditorQmlPathsSourceIds)
{
    using Storage::Synchronization::PropertyEditorQmlPath;
    std::sort(paths.begin(), paths.end(), [](auto &&first, auto &&second) {
        return first.typeId < second.typeId;
    });

    auto range = s->selectPropertyEditorPathsForForSourceIdsStatement.range<PropertyEditorQmlPathView>(
        toIntegers(updatedPropertyEditorQmlPathsSourceIds));

    auto compareKey = [](const PropertyEditorQmlPathView &view,
                         const PropertyEditorQmlPath &value) -> long long {
        return view.typeId - value.typeId;
    };

    auto insert = [&](const PropertyEditorQmlPath &path) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert property editor paths"_t,
                                   projectStorageCategory(),
                                   keyValue("property editor qml path", path)};

        if (path.typeId)
            s->insertPropertyEditorPathStatement.write(path.typeId, path.pathId, path.directoryId);
    };

    auto update = [&](const PropertyEditorQmlPathView &view, const PropertyEditorQmlPath &value) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"update property editor paths"_t,
                                   projectStorageCategory(),
                                   keyValue("property editor qml path", value),
                                   keyValue("property editor qml path view", view)};

        if (value.pathId != view.pathId || value.directoryId != view.directoryId) {
            s->updatePropertyEditorPathsStatement.write(value.typeId, value.pathId, value.directoryId);

            tracer.end(keyValue("updated", "yes"));

            return Sqlite::UpdateChange::Update;
        }
        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const PropertyEditorQmlPathView &view) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove property editor paths"_t,
                                   projectStorageCategory(),
                                   keyValue("property editor qml path view", view)};

        s->deletePropertyEditorPathStatement.write(view.typeId);
    };

    Sqlite::insertUpdateDelete(range, paths, compareKey, insert, update, remove);
}

void ProjectStorage::synchronizePropertyEditorQmlPaths(
    Storage::Synchronization::PropertyEditorQmlPaths &paths,
    SourceIds updatedPropertyEditorQmlPathsSourceIds)
{
    NanotraceHR::Tracer tracer{"synchronize property editor qml paths"_t, projectStorageCategory()};

    addTypeIdToPropertyEditorQmlPaths(paths);
    synchronizePropertyEditorPaths(paths, updatedPropertyEditorQmlPathsSourceIds);
}

void ProjectStorage::synchronizeFunctionDeclarations(
    TypeId typeId, Storage::Synchronization::FunctionDeclarations &functionsDeclarations)
{
    NanotraceHR::Tracer tracer{"synchronize function declaration"_t, projectStorageCategory()};

    std::sort(functionsDeclarations.begin(),
              functionsDeclarations.end(),
              [](auto &&first, auto &&second) {
                  auto compare = Sqlite::compare(first.name, second.name);

                  if (compare == 0) {
                      Utils::PathString firstSignature{createJson(first.parameters)};
                      Utils::PathString secondSignature{createJson(second.parameters)};

                      return Sqlite::compare(firstSignature, secondSignature) < 0;
                  }

                  return compare < 0;
              });

    auto range = s->selectFunctionDeclarationsForTypeIdStatement
                     .range<Storage::Synchronization::FunctionDeclarationView>(typeId);

    auto compareKey = [](const Storage::Synchronization::FunctionDeclarationView &view,
                         const Storage::Synchronization::FunctionDeclaration &value) {
        auto nameKey = Sqlite::compare(view.name, value.name);
        if (nameKey != 0)
            return nameKey;

        Utils::PathString valueSignature{createJson(value.parameters)};

        return Sqlite::compare(view.signature, valueSignature);
    };

    auto insert = [&](const Storage::Synchronization::FunctionDeclaration &value) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert function declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("function declaration", value)};

        Utils::PathString signature{createJson(value.parameters)};

        s->insertFunctionDeclarationStatement.write(typeId, value.name, value.returnTypeName, signature);
    };

    auto update = [&](const Storage::Synchronization::FunctionDeclarationView &view,
                      const Storage::Synchronization::FunctionDeclaration &value) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"update function declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("function declaration", value),
                                   keyValue("function declaration view", view)};

        Utils::PathString signature{createJson(value.parameters)};

        if (value.returnTypeName == view.returnTypeName && signature == view.signature)
            return Sqlite::UpdateChange::No;

        s->updateFunctionDeclarationStatement.write(view.id, value.returnTypeName, signature);

        tracer.end(keyValue("updated", "yes"));

        return Sqlite::UpdateChange::Update;
    };

    auto remove = [&](const Storage::Synchronization::FunctionDeclarationView &view) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove function declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("function declaration view", view)};

        s->deleteFunctionDeclarationStatement.write(view.id);
    };

    Sqlite::insertUpdateDelete(range, functionsDeclarations, compareKey, insert, update, remove);
}

void ProjectStorage::synchronizeSignalDeclarations(
    TypeId typeId, Storage::Synchronization::SignalDeclarations &signalDeclarations)
{
    NanotraceHR::Tracer tracer{"synchronize signal declaration"_t, projectStorageCategory()};

    std::sort(signalDeclarations.begin(), signalDeclarations.end(), [](auto &&first, auto &&second) {
        auto compare = Sqlite::compare(first.name, second.name);

        if (compare == 0) {
            Utils::PathString firstSignature{createJson(first.parameters)};
            Utils::PathString secondSignature{createJson(second.parameters)};

            return Sqlite::compare(firstSignature, secondSignature) < 0;
        }

        return compare < 0;
    });

    auto range = s->selectSignalDeclarationsForTypeIdStatement
                     .range<Storage::Synchronization::SignalDeclarationView>(typeId);

    auto compareKey = [](const Storage::Synchronization::SignalDeclarationView &view,
                         const Storage::Synchronization::SignalDeclaration &value) {
        auto nameKey = Sqlite::compare(view.name, value.name);
        if (nameKey != 0)
            return nameKey;

        Utils::PathString valueSignature{createJson(value.parameters)};

        return Sqlite::compare(view.signature, valueSignature);
    };

    auto insert = [&](const Storage::Synchronization::SignalDeclaration &value) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert signal declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("signal declaration", value)};

        Utils::PathString signature{createJson(value.parameters)};

        s->insertSignalDeclarationStatement.write(typeId, value.name, signature);
    };

    auto update = [&]([[maybe_unused]] const Storage::Synchronization::SignalDeclarationView &view,
                      [[maybe_unused]] const Storage::Synchronization::SignalDeclaration &value) {
        return Sqlite::UpdateChange::No;
    };

    auto remove = [&](const Storage::Synchronization::SignalDeclarationView &view) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove signal declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("signal declaration view", view)};

        s->deleteSignalDeclarationStatement.write(view.id);
    };

    Sqlite::insertUpdateDelete(range, signalDeclarations, compareKey, insert, update, remove);
}

Utils::PathString ProjectStorage::createJson(
    const Storage::Synchronization::EnumeratorDeclarations &enumeratorDeclarations)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"create json from enumerator declarations"_t, projectStorageCategory()};

    Utils::PathString json;
    json.append("{");

    Utils::SmallStringView comma{"\""};

    for (const auto &enumerator : enumeratorDeclarations) {
        json.append(comma);
        comma = ",\"";
        json.append(enumerator.name);
        if (enumerator.hasValue) {
            json.append("\":\"");
            json.append(Utils::SmallString::number(enumerator.value));
            json.append("\"");
        } else {
            json.append("\":null");
        }
    }

    json.append("}");

    return json;
}

void ProjectStorage::synchronizeEnumerationDeclarations(
    TypeId typeId, Storage::Synchronization::EnumerationDeclarations &enumerationDeclarations)
{
    NanotraceHR::Tracer tracer{"synchronize enumeration declaration"_t, projectStorageCategory()};

    std::sort(enumerationDeclarations.begin(),
              enumerationDeclarations.end(),
              [](auto &&first, auto &&second) {
                  return Sqlite::compare(first.name, second.name) < 0;
              });

    auto range = s->selectEnumerationDeclarationsForTypeIdStatement
                     .range<Storage::Synchronization::EnumerationDeclarationView>(typeId);

    auto compareKey = [](const Storage::Synchronization::EnumerationDeclarationView &view,
                         const Storage::Synchronization::EnumerationDeclaration &value) {
        return Sqlite::compare(view.name, value.name);
    };

    auto insert = [&](const Storage::Synchronization::EnumerationDeclaration &value) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"insert enumeration declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("enumeration declaration", value)};

        Utils::PathString signature{createJson(value.enumeratorDeclarations)};

        s->insertEnumerationDeclarationStatement.write(typeId, value.name, signature);
    };

    auto update = [&](const Storage::Synchronization::EnumerationDeclarationView &view,
                      const Storage::Synchronization::EnumerationDeclaration &value) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"update enumeration declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("enumeration declaration", value),
                                   keyValue("enumeration declaration view", view)};

        Utils::PathString enumeratorDeclarations{createJson(value.enumeratorDeclarations)};

        if (enumeratorDeclarations == view.enumeratorDeclarations)
            return Sqlite::UpdateChange::No;

        s->updateEnumerationDeclarationStatement.write(view.id, enumeratorDeclarations);

        tracer.end(keyValue("updated", "yes"));

        return Sqlite::UpdateChange::Update;
    };

    auto remove = [&](const Storage::Synchronization::EnumerationDeclarationView &view) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"remove enumeration declaration"_t,
                                   projectStorageCategory(),
                                   keyValue("enumeration declaration view", view)};

        s->deleteEnumerationDeclarationStatement.write(view.id);
    };

    Sqlite::insertUpdateDelete(range, enumerationDeclarations, compareKey, insert, update, remove);
}

void ProjectStorage::extractExportedTypes(TypeId typeId,
                                          const Storage::Synchronization::Type &type,
                                          Storage::Synchronization::ExportedTypes &exportedTypes)
{
    for (const auto &exportedType : type.exportedTypes)
        exportedTypes.emplace_back(exportedType.name, exportedType.version, typeId, exportedType.moduleId);
}

TypeId ProjectStorage::declareType(Storage::Synchronization::Type &type)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"declare type"_t,
                               projectStorageCategory(),
                               keyValue("source id", type.sourceId),
                               keyValue("type name", type.typeName)};

    if (type.typeName.isEmpty()) {
        type.typeId = s->selectTypeIdBySourceIdStatement.value<TypeId>(type.sourceId);

        tracer.end(keyValue("type id", type.typeId));

        return type.typeId;
    }

    type.typeId = s->insertTypeStatement.value<TypeId>(type.sourceId, type.typeName);

    if (!type.typeId)
        type.typeId = s->selectTypeIdBySourceIdAndNameStatement.value<TypeId>(type.sourceId,
                                                                              type.typeName);

    tracer.end(keyValue("type id", type.typeId));

    return type.typeId;
}

void ProjectStorage::syncDeclarations(Storage::Synchronization::Type &type,
                                      AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
                                      AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
                                      PropertyDeclarationIds &propertyDeclarationIds)
{
    NanotraceHR::Tracer tracer{"synchronize declaration per type"_t, projectStorageCategory()};

    if (type.changeLevel == Storage::Synchronization::ChangeLevel::Minimal)
        return;

    synchronizePropertyDeclarations(type.typeId,
                                    type.propertyDeclarations,
                                    type.sourceId,
                                    insertedAliasPropertyDeclarations,
                                    updatedAliasPropertyDeclarations,
                                    propertyDeclarationIds);
    synchronizeFunctionDeclarations(type.typeId, type.functionDeclarations);
    synchronizeSignalDeclarations(type.typeId, type.signalDeclarations);
    synchronizeEnumerationDeclarations(type.typeId, type.enumerationDeclarations);
}

void ProjectStorage::syncDeclarations(Storage::Synchronization::Types &types,
                                      AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
                                      AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
                                      PropertyDeclarations &relinkablePropertyDeclarations)
{
    NanotraceHR::Tracer tracer{"synchronize declaration"_t, projectStorageCategory()};

    PropertyDeclarationIds propertyDeclarationIds;
    propertyDeclarationIds.reserve(types.size() * 10);

    for (auto &&type : types)
        syncDeclarations(type,
                         insertedAliasPropertyDeclarations,
                         updatedAliasPropertyDeclarations,
                         propertyDeclarationIds);

    removeRelinkableEntries(relinkablePropertyDeclarations,
                            propertyDeclarationIds,
                            PropertyCompare<PropertyDeclaration>{});
}

void ProjectStorage::syncDefaultProperties(Storage::Synchronization::Types &types)
{
    NanotraceHR::Tracer tracer{"synchronize default properties"_t, projectStorageCategory()};

    auto range = s->selectTypesWithDefaultPropertyStatement.range<TypeWithDefaultPropertyView>();

    auto compareKey = [](const TypeWithDefaultPropertyView &view,
                         const Storage::Synchronization::Type &value) {
        return view.typeId - value.typeId;
    };

    auto insert = [&](const Storage::Synchronization::Type &) {

    };

    auto update = [&](const TypeWithDefaultPropertyView &view,
                      const Storage::Synchronization::Type &value) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"synchronize default properties by update"_t,
                                   projectStorageCategory(),
                                   keyValue("type id", value.typeId),
                                   keyValue("value", value),
                                   keyValue("view", view)};

        PropertyDeclarationId valueDefaultPropertyId;
        if (value.defaultPropertyName.size())
            valueDefaultPropertyId = fetchPropertyDeclarationByTypeIdAndNameUngarded(value.typeId,
                                                                                     value.defaultPropertyName)
                                         .propertyDeclarationId;

        if (compareInvalidAreTrue(valueDefaultPropertyId, view.defaultPropertyId))
            return Sqlite::UpdateChange::No;

        s->updateDefaultPropertyIdStatement.write(value.typeId, valueDefaultPropertyId);

        tracer.end(keyValue("updated", "yes"),
                   keyValue("default property id", valueDefaultPropertyId));

        return Sqlite::UpdateChange::Update;
    };

    auto remove = [&](const TypeWithDefaultPropertyView &) {};

    Sqlite::insertUpdateDelete(range, types, compareKey, insert, update, remove);
}

void ProjectStorage::resetDefaultPropertiesIfChanged(Storage::Synchronization::Types &types)
{
    NanotraceHR::Tracer tracer{"reset changed default properties"_t, projectStorageCategory()};

    auto range = s->selectTypesWithDefaultPropertyStatement.range<TypeWithDefaultPropertyView>();

    auto compareKey = [](const TypeWithDefaultPropertyView &view,
                         const Storage::Synchronization::Type &value) {
        return view.typeId - value.typeId;
    };

    auto insert = [&](const Storage::Synchronization::Type &) {

    };

    auto update = [&](const TypeWithDefaultPropertyView &view,
                      const Storage::Synchronization::Type &value) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"reset changed default properties by update"_t,
                                   projectStorageCategory(),
                                   keyValue("type id", value.typeId),
                                   keyValue("value", value),
                                   keyValue("view", view)};

        PropertyDeclarationId valueDefaultPropertyId;
        if (value.defaultPropertyName.size()) {
            auto optionalValueDefaultPropertyId = fetchOptionalPropertyDeclarationByTypeIdAndNameUngarded(
                value.typeId, value.defaultPropertyName);
            if (optionalValueDefaultPropertyId)
                valueDefaultPropertyId = optionalValueDefaultPropertyId->propertyDeclarationId;
        }

        if (compareInvalidAreTrue(valueDefaultPropertyId, view.defaultPropertyId))
            return Sqlite::UpdateChange::No;

        s->updateDefaultPropertyIdStatement.write(value.typeId, Sqlite::NullValue{});

        tracer.end(keyValue("updated", "yes"));

        return Sqlite::UpdateChange::Update;
    };

    auto remove = [&](const TypeWithDefaultPropertyView &) {};

    Sqlite::insertUpdateDelete(range, types, compareKey, insert, update, remove);
}

void ProjectStorage::checkForPrototypeChainCycle(TypeId typeId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"check for prototype chain cycle"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto callback = [=](TypeId currentTypeId) {
        if (typeId == currentTypeId)
            throw PrototypeChainCycle{};
    };

    s->selectPrototypeAndExtensionIdsStatement.readCallback(callback, typeId);
}

void ProjectStorage::checkForAliasChainCycle(PropertyDeclarationId propertyDeclarationId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"check for alias chain cycle"_t,
                               projectStorageCategory(),
                               keyValue("property declaration id", propertyDeclarationId)};
    auto callback = [=](PropertyDeclarationId currentPropertyDeclarationId) {
        if (propertyDeclarationId == currentPropertyDeclarationId)
            throw AliasChainCycle{};
    };

    s->selectPropertyDeclarationIdsForAliasChainStatement.readCallback(callback,
                                                                       propertyDeclarationId);
}

std::pair<TypeId, ImportedTypeNameId> ProjectStorage::fetchImportedTypeNameIdAndTypeId(
    const Storage::Synchronization::ImportedTypeName &typeName, SourceId sourceId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch imported type name id and type id"_t,
                               projectStorageCategory(),
                               keyValue("imported type name", typeName),
                               keyValue("source id", sourceId)};

    TypeId typeId;
    ImportedTypeNameId typeNameId;
    if (!std::visit([](auto &&typeName_) -> bool { return typeName_.name.isEmpty(); }, typeName)) {
        typeNameId = fetchImportedTypeNameId(typeName, sourceId);

        typeId = fetchTypeId(typeNameId);

        tracer.end(keyValue("type id", typeId), keyValue("type name id", typeNameId));

        if (!typeId)
            throw TypeNameDoesNotExists{fetchImportedTypeName(typeNameId), sourceId};
    }

    return {typeId, typeNameId};
}

void ProjectStorage::syncPrototypeAndExtension(Storage::Synchronization::Type &type, TypeIds &typeIds)
{
    if (type.changeLevel == Storage::Synchronization::ChangeLevel::Minimal)
        return;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"synchronize prototype and extension"_t,
                               projectStorageCategory(),
                               keyValue("prototype", type.prototype),
                               keyValue("extension", type.extension),
                               keyValue("type id", type.typeId),
                               keyValue("source id", type.sourceId)};

    auto [prototypeId, prototypeTypeNameId] = fetchImportedTypeNameIdAndTypeId(type.prototype,
                                                                               type.sourceId);
    auto [extensionId, extensionTypeNameId] = fetchImportedTypeNameIdAndTypeId(type.extension,
                                                                               type.sourceId);

    s->updatePrototypeAndExtensionStatement.write(type.typeId,
                                                  prototypeId,
                                                  prototypeTypeNameId,
                                                  extensionId,
                                                  extensionTypeNameId);

    if (prototypeId || extensionId)
        checkForPrototypeChainCycle(type.typeId);

    typeIds.push_back(type.typeId);

    tracer.end(keyValue("prototype id", prototypeId),
               keyValue("prototype type name id", prototypeTypeNameId),
               keyValue("extension id", extensionId),
               keyValue("extension type name id", extensionTypeNameId));
}

void ProjectStorage::syncPrototypesAndExtensions(Storage::Synchronization::Types &types,
                                                 Prototypes &relinkablePrototypes,
                                                 Prototypes &relinkableExtensions)
{
    NanotraceHR::Tracer tracer{"synchronize prototypes and extensions"_t, projectStorageCategory()};

    TypeIds typeIds;
    typeIds.reserve(types.size());

    for (auto &type : types)
        syncPrototypeAndExtension(type, typeIds);

    removeRelinkableEntries(relinkablePrototypes, typeIds, TypeCompare<Prototype>{});
    removeRelinkableEntries(relinkableExtensions, typeIds, TypeCompare<Prototype>{});
}

ImportId ProjectStorage::fetchImportId(SourceId sourceId, const Storage::Import &import) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch imported type name id"_t,
                               projectStorageCategory(),
                               keyValue("import", import),
                               keyValue("source id", sourceId)};

    ImportId importId;
    if (import.version) {
        importId = s->selectImportIdBySourceIdAndModuleIdAndVersionStatement.value<ImportId>(
            sourceId, import.moduleId, import.version.major.value, import.version.minor.value);
    } else if (import.version.major) {
        importId = s->selectImportIdBySourceIdAndModuleIdAndMajorVersionStatement
                       .value<ImportId>(sourceId, import.moduleId, import.version.major.value);
    } else {
        importId = s->selectImportIdBySourceIdAndModuleIdStatement.value<ImportId>(sourceId,
                                                                                   import.moduleId);
    }

    tracer.end(keyValue("import id", importId));

    return importId;
}

ImportedTypeNameId ProjectStorage::fetchImportedTypeNameId(
    const Storage::Synchronization::ImportedTypeName &name, SourceId sourceId)
{
    struct Inspect
    {
        auto operator()(const Storage::Synchronization::ImportedType &importedType)
        {
            using NanotraceHR::keyValue;
            NanotraceHR::Tracer tracer{"fetch imported type name id"_t,
                                       projectStorageCategory(),
                                       keyValue("imported type name", importedType.name),
                                       keyValue("source id", sourceId),
                                       keyValue("type name kind", "exported"sv)};

            return storage.fetchImportedTypeNameId(Storage::Synchronization::TypeNameKind::Exported,
                                                   sourceId,
                                                   importedType.name);
        }

        auto operator()(const Storage::Synchronization::QualifiedImportedType &importedType)
        {
            using NanotraceHR::keyValue;
            NanotraceHR::Tracer tracer{"fetch imported type name id"_t,
                                       projectStorageCategory(),
                                       keyValue("imported type name", importedType.name),
                                       keyValue("import", importedType.import),
                                       keyValue("type name kind", "qualified exported"sv)};

            ImportId importId = storage.fetchImportId(sourceId, importedType.import);

            auto importedTypeNameId = storage.fetchImportedTypeNameId(
                Storage::Synchronization::TypeNameKind::QualifiedExported, importId, importedType.name);

            tracer.end(keyValue("import id", importId), keyValue("source id", sourceId));

            return importedTypeNameId;
        }

        ProjectStorage &storage;
        SourceId sourceId;
    };

    return std::visit(Inspect{*this, sourceId}, name);
}

TypeId ProjectStorage::fetchTypeId(ImportedTypeNameId typeNameId) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch type id with type name kind"_t,
                               projectStorageCategory(),
                               keyValue("type name id", typeNameId)};

    auto kind = s->selectKindFromImportedTypeNamesStatement.value<Storage::Synchronization::TypeNameKind>(
        typeNameId);

    auto typeId = fetchTypeId(typeNameId, kind);

    tracer.end(keyValue("type id", typeId), keyValue("type name kind", kind));

    return typeId;
}

Utils::SmallString ProjectStorage::fetchImportedTypeName(ImportedTypeNameId typeNameId) const
{
    return s->selectNameFromImportedTypeNamesStatement.value<Utils::SmallString>(typeNameId);
}

TypeId ProjectStorage::fetchTypeId(ImportedTypeNameId typeNameId,
                                   Storage::Synchronization::TypeNameKind kind) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch type id"_t,
                               projectStorageCategory(),
                               keyValue("type name id", typeNameId),
                               keyValue("type name kind", kind)};

    TypeId typeId;
    if (kind == Storage::Synchronization::TypeNameKind::Exported) {
        typeId = s->selectTypeIdForImportedTypeNameNamesStatement.value<TypeId>(typeNameId);
    } else {
        typeId = s->selectTypeIdForQualifiedImportedTypeNameNamesStatement.value<TypeId>(typeNameId);
    }

    tracer.end(keyValue("type id", typeId));

    return typeId;
}

std::optional<ProjectStorage::FetchPropertyDeclarationResult>
ProjectStorage::fetchOptionalPropertyDeclarationByTypeIdAndNameUngarded(TypeId typeId,
                                                                        Utils::SmallStringView name)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch optional property declaration by type id and name ungarded"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("property name", name)};

    auto propertyDeclarationId = fetchPropertyDeclarationId(typeId, name);
    auto propertyDeclaration = s->selectPropertyDeclarationResultByPropertyDeclarationIdStatement
                                   .optionalValue<FetchPropertyDeclarationResult>(
                                       propertyDeclarationId);

    tracer.end(keyValue("property declaration", propertyDeclaration));

    return propertyDeclaration;
}

ProjectStorage::FetchPropertyDeclarationResult ProjectStorage::fetchPropertyDeclarationByTypeIdAndNameUngarded(
    TypeId typeId, Utils::SmallStringView name)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch property declaration by type id and name ungarded"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("property name", name)};

    auto propertyDeclaration = fetchOptionalPropertyDeclarationByTypeIdAndNameUngarded(typeId, name);
    tracer.end(keyValue("property declaration", propertyDeclaration));

    if (propertyDeclaration)
        return *propertyDeclaration;

    throw PropertyNameDoesNotExists{};
}

PropertyDeclarationId ProjectStorage::fetchPropertyDeclarationIdByTypeIdAndNameUngarded(
    TypeId typeId, Utils::SmallStringView name)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch property declaration id by type id and name ungarded"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("property name", name)};

    auto propertyDeclarationId = fetchPropertyDeclarationId(typeId, name);

    tracer.end(keyValue("property declaration id", propertyDeclarationId));

    if (propertyDeclarationId)
        return propertyDeclarationId;

    throw PropertyNameDoesNotExists{};
}

SourceContextId ProjectStorage::readSourceContextId(Utils::SmallStringView sourceContextPath)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"read source context id"_t,
                               projectStorageCategory(),
                               keyValue("source context path", sourceContextPath)};

    auto sourceContextId = s->selectSourceContextIdFromSourceContextsBySourceContextPathStatement
                               .value<SourceContextId>(sourceContextPath);

    tracer.end(keyValue("source context id", sourceContextId));

    return sourceContextId;
}

SourceContextId ProjectStorage::writeSourceContextId(Utils::SmallStringView sourceContextPath)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"write source context id"_t,
                               projectStorageCategory(),
                               keyValue("source context path", sourceContextPath)};

    s->insertIntoSourceContextsStatement.write(sourceContextPath);

    auto sourceContextId = SourceContextId::create(static_cast<int>(database.lastInsertedRowId()));

    tracer.end(keyValue("source context id", sourceContextId));

    return sourceContextId;
}

SourceId ProjectStorage::writeSourceId(SourceContextId sourceContextId,
                                       Utils::SmallStringView sourceName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"write source id"_t,
                               projectStorageCategory(),
                               keyValue("source context id", sourceContextId),
                               keyValue("source name", sourceName)};

    s->insertIntoSourcesStatement.write(sourceContextId, sourceName);

    auto sourceId = SourceId::create(static_cast<int>(database.lastInsertedRowId()));

    tracer.end(keyValue("source id", sourceId));

    return sourceId;
}

SourceId ProjectStorage::readSourceId(SourceContextId sourceContextId,
                                      Utils::SmallStringView sourceName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"read source id"_t,
                               projectStorageCategory(),
                               keyValue("source context id", sourceContextId),
                               keyValue("source name", sourceName)};

    auto sourceId = s->selectSourceIdFromSourcesBySourceContextIdAndSourceNameStatement
                        .value<SourceId>(sourceContextId, sourceName);

    tracer.end(keyValue("source id", sourceId));

    return sourceId;
}

Storage::Synchronization::ExportedTypes ProjectStorage::fetchExportedTypes(TypeId typeId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch exported type"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto exportedTypes = s->selectExportedTypesByTypeIdStatement
                             .values<Storage::Synchronization::ExportedType, 12>(typeId);

    tracer.end(keyValue("exported types", exportedTypes));

    return exportedTypes;
}

Storage::Synchronization::PropertyDeclarations ProjectStorage::fetchPropertyDeclarations(TypeId typeId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch property declarations"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    auto propertyDeclarations = s->selectPropertyDeclarationsByTypeIdStatement
                                    .values<Storage::Synchronization::PropertyDeclaration, 24>(typeId);

    tracer.end(keyValue("property declarations", propertyDeclarations));

    return propertyDeclarations;
}

Storage::Synchronization::FunctionDeclarations ProjectStorage::fetchFunctionDeclarations(TypeId typeId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch signal declarations"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    Storage::Synchronization::FunctionDeclarations functionDeclarations;

    auto callback = [&](Utils::SmallStringView name,
                        Utils::SmallStringView returnType,
                        FunctionDeclarationId functionDeclarationId) {
        auto &functionDeclaration = functionDeclarations.emplace_back(name, returnType);
        functionDeclaration.parameters = s->selectFunctionParameterDeclarationsStatement
                                             .values<Storage::Synchronization::ParameterDeclaration, 8>(
                                                 functionDeclarationId);
    };

    s->selectFunctionDeclarationsForTypeIdWithoutSignatureStatement.readCallback(callback, typeId);

    tracer.end(keyValue("function declarations", functionDeclarations));

    return functionDeclarations;
}

Storage::Synchronization::SignalDeclarations ProjectStorage::fetchSignalDeclarations(TypeId typeId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch signal declarations"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    Storage::Synchronization::SignalDeclarations signalDeclarations;

    auto callback = [&](Utils::SmallStringView name, SignalDeclarationId signalDeclarationId) {
        auto &signalDeclaration = signalDeclarations.emplace_back(name);
        signalDeclaration.parameters = s->selectSignalParameterDeclarationsStatement
                                           .values<Storage::Synchronization::ParameterDeclaration, 8>(
                                               signalDeclarationId);
    };

    s->selectSignalDeclarationsForTypeIdWithoutSignatureStatement.readCallback(callback, typeId);

    tracer.end(keyValue("signal declarations", signalDeclarations));

    return signalDeclarations;
}

Storage::Synchronization::EnumerationDeclarations ProjectStorage::fetchEnumerationDeclarations(TypeId typeId)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch enumeration declarations"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId)};

    Storage::Synchronization::EnumerationDeclarations enumerationDeclarations;

    auto callback = [&](Utils::SmallStringView name,
                        EnumerationDeclarationId enumerationDeclarationId) {
        enumerationDeclarations.emplace_back(
            name,
            s->selectEnumeratorDeclarationStatement
                .values<Storage::Synchronization::EnumeratorDeclaration, 8>(enumerationDeclarationId));
    };

    s->selectEnumerationDeclarationsForTypeIdWithoutEnumeratorDeclarationsStatement
        .readCallback(callback, typeId);

    tracer.end(keyValue("enumeration declarations", enumerationDeclarations));

    return enumerationDeclarations;
}

template<typename... TypeIds>
bool ProjectStorage::isBasedOn_(TypeId typeId, TypeIds... baseTypeIds) const
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on"_t,
                               projectStorageCategory(),
                               keyValue("type id", typeId),
                               keyValue("base type ids", NanotraceHR::array(baseTypeIds...))};

    static_assert(((std::is_same_v<TypeId, TypeIds>) &&...), "Parameter must be a TypeId!");

    if (((typeId == baseTypeIds) || ...)) {
        tracer.end(keyValue("is based on", true));
        return true;
    }

    auto range = s->selectPrototypeAndExtensionIdsStatement.rangeWithTransaction<TypeId>(typeId);

    auto isBasedOn = std::any_of(range.begin(), range.end(), [&](TypeId currentTypeId) {
        return ((currentTypeId == baseTypeIds) || ...);
    });

    tracer.end(keyValue("is based on", isBasedOn));

    return isBasedOn;
}

template<typename Id>
ImportedTypeNameId ProjectStorage::fetchImportedTypeNameId(Storage::Synchronization::TypeNameKind kind,
                                                           Id id,
                                                           Utils::SmallStringView typeName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"fetch imported type name id"_t,
                               projectStorageCategory(),
                               keyValue("imported type name", typeName),
                               keyValue("kind", kind)};

    auto importedTypeNameId = s->selectImportedTypeNameIdStatement.value<ImportedTypeNameId>(kind,
                                                                                             id,
                                                                                             typeName);

    if (!importedTypeNameId)
        importedTypeNameId = s->insertImportedTypeNameIdStatement.value<ImportedTypeNameId>(kind,
                                                                                            id,
                                                                                            typeName);

    tracer.end(keyValue("imported type name id", importedTypeNameId));

    return importedTypeNameId;
}

} // namespace QmlDesigner
