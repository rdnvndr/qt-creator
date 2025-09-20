// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"
#include "task.h"

#include <coreplugin/featureprovider.h>

#include <utils/store.h>

#include <QSet>

#include <memory>

namespace Utils {
class Environment;
class MacroExpander;
class OutputLineParser;
} // namespace Utils

namespace ProjectExplorer {
class Project;

namespace Internal {
class KitManagerPrivate;
class KitModel;
class KitPrivate;
} // namespace Internal

/**
 * @brief The Kit class
 *
 * The kit holds a set of values defining a system targeted by the software
 * under development.
 */
class PROJECTEXPLORER_EXPORT Kit
{
public:
    using Predicate = std::function<bool(const Kit *)>;
    static Predicate defaultPredicate();

    explicit Kit(Utils::Id id = {});
    explicit Kit(const Utils::Store &data);
    ~Kit();

    // Do not trigger evaluations
    void blockNotification();
    // Trigger evaluations again.
    void unblockNotification();

    bool isValid() const;
    bool hasWarning() const;
    Tasks validate() const;
    void fix(); // Fix the individual kit information: Make sure it contains a valid value.
                // Fix will not look at other information in the kit!
    void setup(); // Apply advanced magic(TM). Used only once on each kit during initial setup.
    void upgrade(); // Upgrade settings to new syntax (if appropriate).

    QString unexpandedDisplayName() const;
    QString displayName() const;
    void setUnexpandedDisplayName(const QString &name);

    QString fileSystemFriendlyName() const;
    QString customFileSystemFriendlyName() const;
    void setCustomFileSystemFriendlyName(const QString &fileSystemFriendlyName);

    bool isAutoDetected() const;
    QString autoDetectionSource() const;
    bool isSdkProvided() const;
    Utils::Id id() const;

    // The higher the weight, the more aspects have sensible values for this kit.
    // For instance, a kit where a matching debugger was found for the toolchain will have a
    // higher weight than one whose toolchain does not match a known debugger, assuming
    // all other aspects are equal.
    int weight() const;

    QIcon icon() const; // Raw device icon, independent of warning or error.
    QIcon displayIcon() const; // Error or warning or device icon.
    Utils::FilePath iconPath() const;
    void setIconPath(const Utils::FilePath &path);
    void setDeviceTypeForIcon(Utils::Id deviceType);

    QList<Utils::Id> allKeys() const;
    QVariant value(Utils::Id key, const QVariant &unset = QVariant()) const;
    bool hasValue(Utils::Id key) const;
    void setValue(Utils::Id key, const QVariant &value);
    void setValueSilently(Utils::Id key, const QVariant &value);
    void removeKey(Utils::Id key);
    void removeKeySilently(Utils::Id key);
    bool isSticky(Utils::Id id) const;

    bool isDataEqual(const Kit *other) const;
    bool isEqual(const Kit *other) const;

    void addToBuildEnvironment(Utils::Environment &env) const;
    Utils::Environment buildEnvironment() const;

    void addToRunEnvironment(Utils::Environment &env) const;
    Utils::Environment runEnvironment() const;

    QList<Utils::OutputLineParser *> createOutputParsers() const;
    QString moduleForHeader(const QString &className) const;

    QString toHtml(const Tasks &additional = Tasks(), const QString &extraText = QString()) const;
    Kit *clone(bool keepName = false) const;
    void copyFrom(const Kit *k);

    // Note: Stickyness is *not* saved!
    void setAutoDetected(bool detected);
    void setAutoDetectionSource(const QString &autoDetectionSource);
    void makeSticky();
    void setSticky(Utils::Id id, bool b);
    void makeUnSticky();

    void setMutable(Utils::Id id, bool b);
    bool isMutable(Utils::Id id) const;

    bool isReplacementKit() const;

    void setRelevantAspects(const QSet<Utils::Id> &relevant);
    QSet<Utils::Id> relevantAspects() const;
    void setIrrelevantAspects(const QSet<Utils::Id> &irrelevant);
    QSet<Utils::Id> irrelevantAspects() const;
    bool isAspectRelevant(const Utils::Id &aspect) const;

    QSet<Utils::Id> supportedPlatforms() const;
    QSet<Utils::Id> availableFeatures() const;
    bool hasFeatures(const QSet<Utils::Id> &features) const;
    Utils::MacroExpander *macroExpander() const;

    QString newKitName(const QList<Kit *> &allKits) const;
    static QString newKitName(const QString &name, const QList<Kit *> &allKits);

private:
    static void copyKitCommon(Kit *target, const Kit *source);
    void setSdkProvided(bool sdkProvided);

    Kit(const Kit &other) = delete;
    void operator=(const Kit &other) = delete;

    void kitUpdated();

    Utils::Store toMap() const;

    const std::unique_ptr<Internal::KitPrivate> d;

    friend class KitAspectFactory;
    friend class KitManager;
    friend class Internal::KitManagerPrivate;
    friend class Internal::KitModel; // needed for setAutoDetected() when cloning kits
};

class KitGuard
{
public:
    KitGuard(Kit *k) : m_kit(k)
    { k->blockNotification(); }

    ~KitGuard() { m_kit->unblockNotification(); }
private:
    Kit *m_kit;
};

using TasksGenerator = std::function<Tasks(const Kit *)>;

PROJECTEXPLORER_EXPORT Kit *activeKit(const Project *project);
PROJECTEXPLORER_EXPORT Kit *activeKitForActiveProject();
PROJECTEXPLORER_EXPORT Kit *activeKitForCurrentProject();

} // namespace ProjectExplorer

Q_DECLARE_METATYPE(ProjectExplorer::Kit *)
