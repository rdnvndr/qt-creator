// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "commontypecache.h"
#include "projectstorageexceptions.h"
#include "projectstorageinterface.h"
#include "projectstoragetypes.h"
#include "sourcepathcachetypes.h"
#include "storagecache.h"

#include <tracing/qmldesignertracing.h>

#include <sqlitealgorithms.h>
#include <sqlitedatabase.h>
#include <sqlitetable.h>
#include <sqlitetransaction.h>

#include <utils/algorithm.h>
#include <utils/set_algorithm.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace QmlDesigner {

using namespace NanotraceHR::Literals;

using ProjectStorageTracing::projectStorageCategory;

class ProjectStorage final : public ProjectStorageInterface
{
    using Database = Sqlite::Database;
    friend Storage::Info::CommonTypeCache<ProjectStorageType>;

public:
    ProjectStorage(Database &database, bool isInitialized);
    ~ProjectStorage();

    void synchronize(Storage::Synchronization::SynchronizationPackage package) override;

    void synchronizeDocumentImports(Storage::Imports imports, SourceId sourceId) override;

    void addObserver(ProjectStorageObserver *observer) override;

    void removeObserver(ProjectStorageObserver *observer) override;

    ModuleId moduleId(Utils::SmallStringView moduleName) const override;

    Utils::SmallString moduleName(ModuleId moduleId) const override;

    TypeId typeId(ModuleId moduleId,
                  Utils::SmallStringView exportedTypeName,
                  Storage::Version version) const override;

    TypeId typeId(ImportedTypeNameId typeNameId) const override;

    QVarLengthArray<TypeId, 256> typeIds(ModuleId moduleId) const override;

    Storage::Info::ExportedTypeNames exportedTypeNames(TypeId typeId) const override;

    Storage::Info::ExportedTypeNames exportedTypeNames(TypeId typeId, SourceId sourceId) const override;

    ImportId importId(const Storage::Import &import) const override;

    ImportedTypeNameId importedTypeNameId(ImportId importId, Utils::SmallStringView typeName) override;

    ImportedTypeNameId importedTypeNameId(SourceId sourceId, Utils::SmallStringView typeName) override;

    QVarLengthArray<PropertyDeclarationId, 128> propertyDeclarationIds(TypeId typeId) const override;

    QVarLengthArray<PropertyDeclarationId, 128> localPropertyDeclarationIds(TypeId typeId) const override;

    PropertyDeclarationId propertyDeclarationId(TypeId typeId,
                                                Utils::SmallStringView propertyName) const override;

    PropertyDeclarationId localPropertyDeclarationId(TypeId typeId,
                                                     Utils::SmallStringView propertyName) const;

    PropertyDeclarationId defaultPropertyDeclarationId(TypeId typeId) const override;

    std::optional<Storage::Info::PropertyDeclaration> propertyDeclaration(
        PropertyDeclarationId propertyDeclarationId) const override;

    std::optional<Storage::Info::Type> type(TypeId typeId) const override;

    Utils::PathString typeIconPath(TypeId typeId) const override;

    Storage::Info::TypeHints typeHints(TypeId typeId) const override;

    SmallSourceIds<4> typeAnnotationSourceIds(SourceId directoryId) const override;

    SmallSourceIds<64> typeAnnotationDirectorySourceIds() const override;

    Storage::Info::ItemLibraryEntries itemLibraryEntries(TypeId typeId) const override;

    Storage::Info::ItemLibraryEntries itemLibraryEntries(ImportId importId) const;

    Storage::Info::ItemLibraryEntries itemLibraryEntries(SourceId sourceId) const override;

    Storage::Info::ItemLibraryEntries allItemLibraryEntries() const override;

    std::vector<Utils::SmallString> signalDeclarationNames(TypeId typeId) const override;

    std::vector<Utils::SmallString> functionDeclarationNames(TypeId typeId) const override;

    std::optional<Utils::SmallString> propertyName(PropertyDeclarationId propertyDeclarationId) const override;

    const Storage::Info::CommonTypeCache<ProjectStorageType> &commonTypeCache() const override
    {
        return commonTypeCache_;
    }

    template<const char *moduleName, const char *typeName>
    TypeId commonTypeId() const
    {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get type id from common type cache"_t,
                                   projectStorageCategory(),
                                   keyValue("module name", std::string_view{moduleName}),
                                   keyValue("type name", std::string_view{typeName})};

        auto typeId = commonTypeCache_.typeId<moduleName, typeName>();

        tracer.end(keyValue("type id", typeId));

        return typeId;
    }

    template<typename BuiltinType>
    TypeId builtinTypeId() const
    {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get builtin type id from common type cache"_t,
                                   projectStorageCategory()};

        auto typeId = commonTypeCache_.builtinTypeId<BuiltinType>();

        tracer.end(keyValue("type id", typeId));

        return typeId;
    }

    template<const char *builtinType>
    TypeId builtinTypeId() const
    {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get builtin type id from common type cache"_t,
                                   projectStorageCategory()};

        auto typeId = commonTypeCache_.builtinTypeId<builtinType>();

        tracer.end(keyValue("type id", typeId));

        return typeId;
    }

    SmallTypeIds<16> prototypeIds(TypeId type) const override;

    SmallTypeIds<16> prototypeAndSelfIds(TypeId typeId) const override;

    SmallTypeIds<64> heirIds(TypeId typeId) const override;

    template<typename... TypeIds>
    bool isBasedOn_(TypeId typeId, TypeIds... baseTypeIds) const;

    bool isBasedOn(TypeId) const;

    bool isBasedOn(TypeId typeId, TypeId id1) const override;

    bool isBasedOn(TypeId typeId, TypeId id1, TypeId id2) const override;

    bool isBasedOn(TypeId typeId, TypeId id1, TypeId id2, TypeId id3) const override;

    bool isBasedOn(TypeId typeId, TypeId id1, TypeId id2, TypeId id3, TypeId id4) const override;

    bool isBasedOn(TypeId typeId, TypeId id1, TypeId id2, TypeId id3, TypeId id4, TypeId id5) const override;

    bool isBasedOn(TypeId typeId, TypeId id1, TypeId id2, TypeId id3, TypeId id4, TypeId id5, TypeId id6)
        const override;

    bool isBasedOn(TypeId typeId,
                   TypeId id1,
                   TypeId id2,
                   TypeId id3,
                   TypeId id4,
                   TypeId id5,
                   TypeId id6,
                   TypeId id7) const override;

    TypeId fetchTypeIdByExportedName(Utils::SmallStringView name) const;

    TypeId fetchTypeIdByModuleIdsAndExportedName(ModuleIds moduleIds,
                                                 Utils::SmallStringView name) const;

    TypeId fetchTypeIdByName(SourceId sourceId, Utils::SmallStringView name);

    Storage::Synchronization::Type fetchTypeByTypeId(TypeId typeId);

    Storage::Synchronization::Types fetchTypes();

    SourceContextId fetchSourceContextIdUnguarded(Utils::SmallStringView sourceContextPath);

    SourceContextId fetchSourceContextId(Utils::SmallStringView sourceContextPath);

    Utils::PathString fetchSourceContextPath(SourceContextId sourceContextId) const;

    Cache::SourceContexts fetchAllSourceContexts() const;

    SourceId fetchSourceId(SourceContextId sourceContextId, Utils::SmallStringView sourceName);

    Cache::SourceNameAndSourceContextId fetchSourceNameAndSourceContextId(SourceId sourceId) const;

    void clearSources();

    SourceContextId fetchSourceContextId(SourceId sourceId) const;

    Cache::Sources fetchAllSources() const;

    SourceId fetchSourceIdUnguarded(SourceContextId sourceContextId,
                                    Utils::SmallStringView sourceName);

    FileStatuses fetchAllFileStatuses() const;

    FileStatus fetchFileStatus(SourceId sourceId) const override;

    std::optional<Storage::Synchronization::ProjectData> fetchProjectData(SourceId sourceId) const override;

    Storage::Synchronization::ProjectDatas fetchProjectDatas(SourceId projectSourceId) const override;

    Storage::Synchronization::ProjectDatas fetchProjectDatas(const SourceIds &projectSourceIds) const;

    void setPropertyEditorPathId(TypeId typeId, SourceId pathId);

    SourceId propertyEditorPathId(TypeId typeId) const override;

    Storage::Imports fetchDocumentImports() const;

    void resetForTestsOnly();

private:
    class ModuleStorageAdapter
    {
    public:
        auto fetchId(const Utils::SmallStringView name) { return storage.fetchModuleId(name); }

        auto fetchValue(ModuleId id) { return storage.fetchModuleName(id); }

        auto fetchAll() { return storage.fetchAllModules(); }

        ProjectStorage &storage;
    };

    class Module : public StorageCacheEntry<Utils::PathString, Utils::SmallStringView, ModuleId>
    {
        using Base = StorageCacheEntry<Utils::PathString, Utils::SmallStringView, ModuleId>;

    public:
        using Base::Base;

        friend bool operator==(const Module &first, const Module &second)
        {
            return &first == &second && first.value == second.value;
        }
    };

    using Modules = std::vector<Module>;

    friend ModuleStorageAdapter;

    static bool moduleNameLess(Utils::SmallStringView first, Utils::SmallStringView second) noexcept;

    using ModuleCache = StorageCache<Utils::PathString,
                                     Utils::SmallStringView,
                                     ModuleId,
                                     ModuleStorageAdapter,
                                     NonLockingMutex,
                                     moduleNameLess,
                                     Module>;

    ModuleId fetchModuleId(Utils::SmallStringView moduleName);

    Utils::PathString fetchModuleName(ModuleId id);

    Modules fetchAllModules() const;

    void callRefreshMetaInfoCallback(const TypeIds &deletedTypeIds);

    class AliasPropertyDeclaration
    {
    public:
        explicit AliasPropertyDeclaration(
            TypeId typeId,
            PropertyDeclarationId propertyDeclarationId,
            ImportedTypeNameId aliasImportedTypeNameId,
            Utils::SmallString aliasPropertyName,
            Utils::SmallString aliasPropertyNameTail,
            PropertyDeclarationId aliasPropertyDeclarationId = PropertyDeclarationId{})
            : typeId{typeId}
            , propertyDeclarationId{propertyDeclarationId}
            , aliasImportedTypeNameId{aliasImportedTypeNameId}
            , aliasPropertyName{std::move(aliasPropertyName)}
            , aliasPropertyNameTail{std::move(aliasPropertyNameTail)}
            , aliasPropertyDeclarationId{aliasPropertyDeclarationId}
        {}

        friend bool operator<(const AliasPropertyDeclaration &first,
                              const AliasPropertyDeclaration &second)
        {
            return std::tie(first.typeId, first.propertyDeclarationId)
                   < std::tie(second.typeId, second.propertyDeclarationId);
        }

        template<typename String>
        friend void convertToString(String &string,
                                    const AliasPropertyDeclaration &aliasPropertyDeclaration)
        {
            using NanotraceHR::dictonary;
            using NanotraceHR::keyValue;
            auto dict = dictonary(
                keyValue("type id", aliasPropertyDeclaration.typeId),
                keyValue("property declaration id", aliasPropertyDeclaration.propertyDeclarationId),
                keyValue("alias imported type name id",
                         aliasPropertyDeclaration.aliasImportedTypeNameId),
                keyValue("alias property name", aliasPropertyDeclaration.aliasPropertyName),
                keyValue("alias property name tail", aliasPropertyDeclaration.aliasPropertyNameTail),
                keyValue("alias property declaration id",
                         aliasPropertyDeclaration.aliasPropertyDeclarationId));

            convertToString(string, dict);
        }

    public:
        TypeId typeId;
        PropertyDeclarationId propertyDeclarationId;
        ImportedTypeNameId aliasImportedTypeNameId;
        Utils::SmallString aliasPropertyName;
        Utils::SmallString aliasPropertyNameTail;
        PropertyDeclarationId aliasPropertyDeclarationId;
    };

    using AliasPropertyDeclarations = std::vector<AliasPropertyDeclaration>;

    class PropertyDeclaration
    {
    public:
        explicit PropertyDeclaration(TypeId typeId,
                                     PropertyDeclarationId propertyDeclarationId,
                                     ImportedTypeNameId importedTypeNameId)
            : typeId{typeId}
            , propertyDeclarationId{propertyDeclarationId}
            , importedTypeNameId{std::move(importedTypeNameId)}
        {}

        friend bool operator<(const PropertyDeclaration &first, const PropertyDeclaration &second)
        {
            return std::tie(first.typeId, first.propertyDeclarationId)
                   < std::tie(second.typeId, second.propertyDeclarationId);
        }

        template<typename String>
        friend void convertToString(String &string, const PropertyDeclaration &propertyDeclaration)
        {
            using NanotraceHR::dictonary;
            using NanotraceHR::keyValue;
            auto dict = dictonary(keyValue("type id", propertyDeclaration.typeId),
                                  keyValue("property declaration id",
                                           propertyDeclaration.propertyDeclarationId),
                                  keyValue("imported type name id",
                                           propertyDeclaration.importedTypeNameId));

            convertToString(string, dict);
        }

    public:
        TypeId typeId;
        PropertyDeclarationId propertyDeclarationId;
        ImportedTypeNameId importedTypeNameId;
    };

    using PropertyDeclarations = std::vector<PropertyDeclaration>;

    class Prototype
    {
    public:
        explicit Prototype(TypeId typeId, ImportedTypeNameId prototypeNameId)
            : typeId{typeId}
            , prototypeNameId{std::move(prototypeNameId)}
        {}

        friend bool operator<(Prototype first, Prototype second)
        {
            return first.typeId < second.typeId;
        }

        template<typename String>
        friend void convertToString(String &string, const Prototype &prototype)
        {
            using NanotraceHR::dictonary;
            using NanotraceHR::keyValue;
            auto dict = dictonary(keyValue("type id", prototype.typeId),
                                  keyValue("prototype name id", prototype.prototypeNameId));

            convertToString(string, dict);
        }

    public:
        TypeId typeId;
        ImportedTypeNameId prototypeNameId;
    };

    using Prototypes = std::vector<Prototype>;

    template<typename Type>
    struct TypeCompare
    {
        bool operator()(const Type &type, TypeId typeId) { return type.typeId < typeId; }

        bool operator()(TypeId typeId, const Type &type) { return typeId < type.typeId; }

        bool operator()(const Type &first, const Type &second)
        {
            return first.typeId < second.typeId;
        }
    };

    template<typename Property>
    struct PropertyCompare
    {
        bool operator()(const Property &property, PropertyDeclarationId id)
        {
            return property.propertyDeclarationId < id;
        }

        bool operator()(PropertyDeclarationId id, const Property &property)
        {
            return id < property.propertyDeclarationId;
        }

        bool operator()(const Property &first, const Property &second)
        {
            return first.propertyDeclarationId < second.propertyDeclarationId;
        }
    };

    SourceIds filterSourceIdsWithoutType(const SourceIds &updatedSourceIds,
                                         SourceIds &sourceIdsOfTypes);

    TypeIds fetchTypeIds(const SourceIds &sourceIds);

    void unique(SourceIds &sourceIds);

    void synchronizeTypeTraits(TypeId typeId, Storage::TypeTraits traits);

    class TypeAnnotationView
    {
    public:
        TypeAnnotationView(TypeId typeId,
                           Utils::SmallStringView iconPath,
                           Utils::SmallStringView itemLibraryJson,
                           Utils::SmallStringView hintsJson)
            : typeId{typeId}
            , iconPath{iconPath}
            , itemLibraryJson{itemLibraryJson}
            , hintsJson{hintsJson}
        {}

        template<typename String>
        friend void convertToString(String &string, const TypeAnnotationView &typeAnnotationView)
        {
            using NanotraceHR::dictonary;
            using NanotraceHR::keyValue;
            auto dict = dictonary(keyValue("type id", typeAnnotationView.typeId),
                                  keyValue("icon path", typeAnnotationView.iconPath),
                                  keyValue("item library json", typeAnnotationView.itemLibraryJson),
                                  keyValue("hints json", typeAnnotationView.hintsJson));

            convertToString(string, dict);
        }

    public:
        TypeId typeId;
        Utils::SmallStringView iconPath;
        Utils::SmallStringView itemLibraryJson;
        Utils::PathString hintsJson;
    };

    void updateTypeIdInTypeAnnotations(Storage::Synchronization::TypeAnnotations &typeAnnotations);

    template<typename Value>
    static Sqlite::ValueView createEmptyAsNull(const Value &value)
    {
        if (value.size())
            return Sqlite::ValueView::create(value);

        return Sqlite::ValueView{};
    }

    void synchronizeTypeAnnotations(Storage::Synchronization::TypeAnnotations &typeAnnotations,
                                    const SourceIds &updatedTypeAnnotationSourceIds);

    void synchronizeTypeTrait(const Storage::Synchronization::Type &type);

    void synchronizeTypes(Storage::Synchronization::Types &types,
                          TypeIds &updatedTypeIds,
                          AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
                          AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
                          AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                          PropertyDeclarations &relinkablePropertyDeclarations,
                          Prototypes &relinkablePrototypes,
                          Prototypes &relinkableExtensions,
                          const SourceIds &updatedSourceIds);

    void synchronizeProjectDatas(Storage::Synchronization::ProjectDatas &projectDatas,
                                 const SourceIds &updatedProjectSourceIds);

    void synchronizeFileStatuses(FileStatuses &fileStatuses, const SourceIds &updatedSourceIds);

    void synchronizeImports(Storage::Imports &imports,
                            const SourceIds &updatedSourceIds,
                            Storage::Imports &moduleDependencies,
                            const SourceIds &updatedModuleDependencySourceIds,
                            Storage::Synchronization::ModuleExportedImports &moduleExportedImports,
                            const ModuleIds &updatedModuleIds);

    void synchromizeModuleExportedImports(
        Storage::Synchronization::ModuleExportedImports &moduleExportedImports,
        const ModuleIds &updatedModuleIds);

    ModuleId fetchModuleIdUnguarded(Utils::SmallStringView name) const override;

    Utils::PathString fetchModuleNameUnguarded(ModuleId id) const;

    void handleAliasPropertyDeclarationsWithPropertyType(
        TypeId typeId, AliasPropertyDeclarations &relinkableAliasPropertyDeclarations);

    void handlePropertyDeclarationWithPropertyType(TypeId typeId,
                                                   PropertyDeclarations &relinkablePropertyDeclarations);

    void handlePrototypes(TypeId prototypeId, Prototypes &relinkablePrototypes);

    void handleExtensions(TypeId extensionId, Prototypes &relinkableExtensions);

    void deleteType(TypeId typeId,
                    AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                    PropertyDeclarations &relinkablePropertyDeclarations,
                    Prototypes &relinkablePrototypes,
                    Prototypes &relinkableExtensions);

    void relinkAliasPropertyDeclarations(AliasPropertyDeclarations &aliasPropertyDeclarations,
                                         const TypeIds &deletedTypeIds);

    void relinkPropertyDeclarations(PropertyDeclarations &relinkablePropertyDeclaration,
                                    const TypeIds &deletedTypeIds);

    template<typename Callable>
    void relinkPrototypes(Prototypes &relinkablePrototypes,
                          const TypeIds &deletedTypeIds,
                          Callable updateStatement)
    {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"relink prototypes"_t,
                                   projectStorageCategory(),
                                   keyValue("relinkable prototypes", relinkablePrototypes),
                                   keyValue("deleted type ids", deletedTypeIds)};

        std::sort(relinkablePrototypes.begin(), relinkablePrototypes.end());

        Utils::set_greedy_difference(
            relinkablePrototypes.cbegin(),
            relinkablePrototypes.cend(),
            deletedTypeIds.begin(),
            deletedTypeIds.end(),
            [&](const Prototype &prototype) {
                TypeId prototypeId = fetchTypeId(prototype.prototypeNameId);

                if (!prototypeId)
                    throw TypeNameDoesNotExists{fetchImportedTypeName(prototype.prototypeNameId)};

                updateStatement(prototype.typeId, prototypeId);
                checkForPrototypeChainCycle(prototype.typeId);
            },
            TypeCompare<Prototype>{});
    }

    void deleteNotUpdatedTypes(const TypeIds &updatedTypeIds,
                               const SourceIds &updatedSourceIds,
                               const TypeIds &typeIdsToBeDeleted,
                               AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                               PropertyDeclarations &relinkablePropertyDeclarations,
                               Prototypes &relinkablePrototypes,
                               Prototypes &relinkableExtensions,
                               TypeIds &deletedTypeIds);

    void relink(AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                PropertyDeclarations &relinkablePropertyDeclarations,
                Prototypes &relinkablePrototypes,
                Prototypes &relinkableExtensions,
                TypeIds &deletedTypeIds);

    PropertyDeclarationId fetchAliasId(TypeId aliasTypeId,
                                       Utils::SmallStringView aliasPropertyName,
                                       Utils::SmallStringView aliasPropertyNameTail);

    void linkAliasPropertyDeclarationAliasIds(const AliasPropertyDeclarations &aliasDeclarations);

    void updateAliasPropertyDeclarationValues(const AliasPropertyDeclarations &aliasDeclarations);

    void checkAliasPropertyDeclarationCycles(const AliasPropertyDeclarations &aliasDeclarations);

    void linkAliases(const AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
                     const AliasPropertyDeclarations &updatedAliasPropertyDeclarations);

    void synchronizeExportedTypes(const TypeIds &updatedTypeIds,
                                  Storage::Synchronization::ExportedTypes &exportedTypes,
                                  AliasPropertyDeclarations &relinkableAliasPropertyDeclarations,
                                  PropertyDeclarations &relinkablePropertyDeclarations,
                                  Prototypes &relinkablePrototypes,
                                  Prototypes &relinkableExtensions);

    void synchronizePropertyDeclarationsInsertAlias(
        AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
        const Storage::Synchronization::PropertyDeclaration &value,
        SourceId sourceId,
        TypeId typeId);

    QVarLengthArray<PropertyDeclarationId, 128> fetchPropertyDeclarationIds(TypeId baseTypeId) const;

    PropertyDeclarationId fetchNextPropertyDeclarationId(TypeId baseTypeId,
                                                         Utils::SmallStringView propertyName) const;

    PropertyDeclarationId fetchPropertyDeclarationId(TypeId typeId,
                                                     Utils::SmallStringView propertyName) const;

    PropertyDeclarationId fetchNextDefaultPropertyDeclarationId(TypeId baseTypeId) const;

    PropertyDeclarationId fetchDefaultPropertyDeclarationId(TypeId typeId) const;

    void synchronizePropertyDeclarationsInsertProperty(
        const Storage::Synchronization::PropertyDeclaration &value, SourceId sourceId, TypeId typeId);

    void synchronizePropertyDeclarationsUpdateAlias(
        AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
        const Storage::Synchronization::PropertyDeclarationView &view,
        const Storage::Synchronization::PropertyDeclaration &value,
        SourceId sourceId);

    Sqlite::UpdateChange synchronizePropertyDeclarationsUpdateProperty(
        const Storage::Synchronization::PropertyDeclarationView &view,
        const Storage::Synchronization::PropertyDeclaration &value,
        SourceId sourceId,
        PropertyDeclarationIds &propertyDeclarationIds);

    void synchronizePropertyDeclarations(
        TypeId typeId,
        Storage::Synchronization::PropertyDeclarations &propertyDeclarations,
        SourceId sourceId,
        AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
        AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
        PropertyDeclarationIds &propertyDeclarationIds);

    class AliasPropertyDeclarationView
    {
    public:
        explicit AliasPropertyDeclarationView(Utils::SmallStringView name,
                                              PropertyDeclarationId id,
                                              PropertyDeclarationId aliasId)
            : name{name}
            , id{id}
            , aliasId{aliasId}
        {}

        template<typename String>
        friend void convertToString(String &string,
                                    const AliasPropertyDeclarationView &aliasPropertyDeclarationView)
        {
            using NanotraceHR::dictonary;
            using NanotraceHR::keyValue;
            auto dict = dictonary(keyValue("name", aliasPropertyDeclarationView.name),
                                  keyValue("id", aliasPropertyDeclarationView.id),
                                  keyValue("alias id", aliasPropertyDeclarationView.aliasId));

            convertToString(string, dict);
        }

    public:
        Utils::SmallStringView name;
        PropertyDeclarationId id;
        PropertyDeclarationId aliasId;
    };

    void resetRemovedAliasPropertyDeclarationsToNull(Storage::Synchronization::Type &type,
                                                     PropertyDeclarationIds &propertyDeclarationIds);

    void resetRemovedAliasPropertyDeclarationsToNull(
        Storage::Synchronization::Types &types,
        AliasPropertyDeclarations &relinkableAliasPropertyDeclarations);

    ImportId insertDocumentImport(const Storage::Import &import,
                                  Storage::Synchronization::ImportKind importKind,
                                  ModuleId sourceModuleId,
                                  ImportId parentImportId);

    void synchronizeDocumentImports(Storage::Imports &imports,
                                    const SourceIds &updatedSourceIds,
                                    Storage::Synchronization::ImportKind importKind);

    static Utils::PathString createJson(const Storage::Synchronization::ParameterDeclarations &parameters);

    TypeId fetchTypeIdByModuleIdAndExportedName(ModuleId moduleId,
                                                Utils::SmallStringView name) const override;

    void addTypeIdToPropertyEditorQmlPaths(Storage::Synchronization::PropertyEditorQmlPaths &paths);

    class PropertyEditorQmlPathView
    {
    public:
        PropertyEditorQmlPathView(TypeId typeId, SourceId pathId, SourceId directoryId)
            : typeId{typeId}
            , pathId{pathId}
            , directoryId{directoryId}
        {}

        template<typename String>
        friend void convertToString(String &string,
                                    const PropertyEditorQmlPathView &propertyEditorQmlPathView)
        {
            using NanotraceHR::dictonary;
            using NanotraceHR::keyValue;
            auto dict = dictonary(keyValue("type id", propertyEditorQmlPathView.typeId),
                                  keyValue("source id", propertyEditorQmlPathView.pathId),
                                  keyValue("directory id", propertyEditorQmlPathView.directoryId));

            convertToString(string, dict);
        }

    public:
        TypeId typeId;
        SourceId pathId;
        SourceId directoryId;
    };

    void synchronizePropertyEditorPaths(Storage::Synchronization::PropertyEditorQmlPaths &paths,
                                        SourceIds updatedPropertyEditorQmlPathsSourceIds);

    void synchronizePropertyEditorQmlPaths(Storage::Synchronization::PropertyEditorQmlPaths &paths,
                                           SourceIds updatedPropertyEditorQmlPathsSourceIds);

    void synchronizeFunctionDeclarations(
        TypeId typeId, Storage::Synchronization::FunctionDeclarations &functionsDeclarations);

    void synchronizeSignalDeclarations(TypeId typeId,
                                       Storage::Synchronization::SignalDeclarations &signalDeclarations);

    static Utils::PathString createJson(
        const Storage::Synchronization::EnumeratorDeclarations &enumeratorDeclarations);

    void synchronizeEnumerationDeclarations(
        TypeId typeId, Storage::Synchronization::EnumerationDeclarations &enumerationDeclarations);

    void extractExportedTypes(TypeId typeId,
                              const Storage::Synchronization::Type &type,
                              Storage::Synchronization::ExportedTypes &exportedTypes);

    TypeId declareType(Storage::Synchronization::Type &type);

    void syncDeclarations(Storage::Synchronization::Type &type,
                          AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
                          AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
                          PropertyDeclarationIds &propertyDeclarationIds);

    template<typename Relinkable, typename Ids, typename Compare>
    void removeRelinkableEntries(std::vector<Relinkable> &relinkables, Ids &ids, Compare compare)
    {
        NanotraceHR::Tracer tracer{"remove relinkable entries"_t, projectStorageCategory()};

        std::vector<Relinkable> newRelinkables;
        newRelinkables.reserve(relinkables.size());

        std::sort(ids.begin(), ids.end());
        std::sort(relinkables.begin(), relinkables.end(), compare);

        Utils::set_greedy_difference(
            relinkables.begin(),
            relinkables.end(),
            ids.cbegin(),
            ids.cend(),
            [&](Relinkable &entry) { newRelinkables.push_back(std::move(entry)); },
            compare);

        relinkables = std::move(newRelinkables);
    }

    void syncDeclarations(Storage::Synchronization::Types &types,
                          AliasPropertyDeclarations &insertedAliasPropertyDeclarations,
                          AliasPropertyDeclarations &updatedAliasPropertyDeclarations,
                          PropertyDeclarations &relinkablePropertyDeclarations);

    class TypeWithDefaultPropertyView
    {
    public:
        TypeWithDefaultPropertyView(TypeId typeId, PropertyDeclarationId defaultPropertyId)
            : typeId{typeId}
            , defaultPropertyId{defaultPropertyId}
        {}

        template<typename String>
        friend void convertToString(String &string, const TypeWithDefaultPropertyView &view)
        {
            using NanotraceHR::dictonary;
            using NanotraceHR::keyValue;
            auto dict = dictonary(keyValue("type id", view.typeId),
                                  keyValue("property id", view.defaultPropertyId));

            convertToString(string, dict);
        }

        TypeId typeId;
        PropertyDeclarationId defaultPropertyId;
    };

    void syncDefaultProperties(Storage::Synchronization::Types &types);

    void resetDefaultPropertiesIfChanged(Storage::Synchronization::Types &types);

    void checkForPrototypeChainCycle(TypeId typeId) const;

    void checkForAliasChainCycle(PropertyDeclarationId propertyDeclarationId) const;

    std::pair<TypeId, ImportedTypeNameId> fetchImportedTypeNameIdAndTypeId(
        const Storage::Synchronization::ImportedTypeName &typeName, SourceId sourceId);

    void syncPrototypeAndExtension(Storage::Synchronization::Type &type, TypeIds &typeIds);

    void syncPrototypesAndExtensions(Storage::Synchronization::Types &types,
                                     Prototypes &relinkablePrototypes,
                                     Prototypes &relinkableExtensions);

    ImportId fetchImportId(SourceId sourceId, const Storage::Import &import) const;

    ImportedTypeNameId fetchImportedTypeNameId(const Storage::Synchronization::ImportedTypeName &name,
                                               SourceId sourceId);

    template<typename Id>
    ImportedTypeNameId fetchImportedTypeNameId(Storage::Synchronization::TypeNameKind kind,
                                               Id id,
                                               Utils::SmallStringView typeName);

    TypeId fetchTypeId(ImportedTypeNameId typeNameId) const;

    Utils::SmallString fetchImportedTypeName(ImportedTypeNameId typeNameId) const;

    TypeId fetchTypeId(ImportedTypeNameId typeNameId,
                       Storage::Synchronization::TypeNameKind kind) const;

    class FetchPropertyDeclarationResult
    {
    public:
        FetchPropertyDeclarationResult(TypeId propertyTypeId,
                                       PropertyDeclarationId propertyDeclarationId,
                                       Storage::PropertyDeclarationTraits propertyTraits)
            : propertyTypeId{propertyTypeId}
            , propertyDeclarationId{propertyDeclarationId}
            , propertyTraits{propertyTraits}
        {}

        template<typename String>
        friend void convertToString(String &string, const FetchPropertyDeclarationResult &result)
        {
            using NanotraceHR::dictonary;
            using NanotraceHR::keyValue;
            auto dict = dictonary(keyValue("property type id", result.propertyTypeId),
                                  keyValue("property declaration id", result.propertyDeclarationId),
                                  keyValue("property traits", result.propertyTraits));

            convertToString(string, dict);
        }

    public:
        TypeId propertyTypeId;
        PropertyDeclarationId propertyDeclarationId;
        Storage::PropertyDeclarationTraits propertyTraits;
    };

    std::optional<FetchPropertyDeclarationResult> fetchOptionalPropertyDeclarationByTypeIdAndNameUngarded(
        TypeId typeId, Utils::SmallStringView name);

    FetchPropertyDeclarationResult fetchPropertyDeclarationByTypeIdAndNameUngarded(
        TypeId typeId, Utils::SmallStringView name);

    PropertyDeclarationId fetchPropertyDeclarationIdByTypeIdAndNameUngarded(TypeId typeId,
                                                                            Utils::SmallStringView name);

    SourceContextId readSourceContextId(Utils::SmallStringView sourceContextPath);

    SourceContextId writeSourceContextId(Utils::SmallStringView sourceContextPath);

    SourceId writeSourceId(SourceContextId sourceContextId, Utils::SmallStringView sourceName);

    SourceId readSourceId(SourceContextId sourceContextId, Utils::SmallStringView sourceName);

    Storage::Synchronization::ExportedTypes fetchExportedTypes(TypeId typeId);

    Storage::Synchronization::PropertyDeclarations fetchPropertyDeclarations(TypeId typeId);

    Storage::Synchronization::FunctionDeclarations fetchFunctionDeclarations(TypeId typeId);

    Storage::Synchronization::SignalDeclarations fetchSignalDeclarations(TypeId typeId);

    Storage::Synchronization::EnumerationDeclarations fetchEnumerationDeclarations(TypeId typeId);

    class Initializer;

    struct Statements;

public:
    Database &database;
    Sqlite::ExclusiveNonThrowingDestructorTransaction<Database> exclusiveTransaction;
    std::unique_ptr<Initializer> initializer;
    mutable ModuleCache moduleCache{ModuleStorageAdapter{*this}};
    Storage::Info::CommonTypeCache<ProjectStorageType> commonTypeCache_{*this};
    QVarLengthArray<ProjectStorageObserver *, 24> observers;
    std::unique_ptr<Statements> s;
};


} // namespace QmlDesigner
