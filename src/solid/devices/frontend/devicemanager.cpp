/*
    SPDX-FileCopyrightText: 2005-2007 Kevin Ottens <ervin@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "devicenotifier.h"
#include "devicemanager_p.h" //krazy:exclude=includes (devicenotifier.h is the header file for this class)

#include "device.h"
#include "device_p.h"
#include "predicate.h"
#include "storageaccess.h"
#include "storagevolume.h"

#include "ifaces/devicemanager.h"
#include "ifaces/device.h"

#include "soliddefs_p.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

Q_GLOBAL_STATIC(Solid::DeviceManagerStorage, globalDeviceStorage)

Q_LOGGING_CATEGORY(DEVICE_MANAGER_LOG, "device manager")

Solid::DeviceManagerPrivate::DeviceManagerPrivate()
    : m_nullDevice(new DevicePrivate(QString()))
{
    loadBackends();

    const QList<QObject *> backends = managerBackends();
    for (QObject *backend : backends) {
        connect(backend, SIGNAL(deviceAdded(QString)),
                this, SLOT(_k_deviceAdded(QString)));
        connect(backend, SIGNAL(deviceRemoved(QString)),
                this, SLOT(_k_deviceRemoved(QString)));
    }
}

Solid::DeviceManagerPrivate::~DeviceManagerPrivate()
{
    const QList<QObject *> backends = managerBackends();
    for (QObject *backend : backends) {
        disconnect(backend, nullptr, this, nullptr);
    }

    // take a copy as m_devicesMap is changed by Solid::DeviceManagerPrivate::_k_destroyed
    const auto deviceMap = m_devicesMap;
    for (QPointer<DevicePrivate> dev : deviceMap) {
        if (!dev.data()->ref.deref()) {
            delete dev.data();
        }
    }

    m_devicesMap.clear();
}

QList<Solid::Device> Solid::Device::allDevices()
{
    QList<Device> list;
    const QList<QObject *> backends = globalDeviceStorage->managerBackends();

    for (QObject *backendObj : backends) {
        Ifaces::DeviceManager *backend = qobject_cast<Ifaces::DeviceManager *>(backendObj);

        if (backend == nullptr) {
            continue;
        }

        const QStringList udis = backend->allDevices();
        for (const QString &udi : udis) {
            list.append(Device(udi));
        }
    }

    return list;
}

QList<Solid::Device> Solid::Device::listFromQuery(const QString &predicate,
        const QString &parentUdi)
{
    Predicate p = Predicate::fromString(predicate);

    if (p.isValid()) {
        return listFromQuery(p, parentUdi);
    } else {
        return QList<Device>();
    }
}

QList<Solid::Device> Solid::Device::listFromType(const DeviceInterface::Type &type,
        const QString &parentUdi)
{
    QList<Device> list;
    const QList<QObject *> backends = globalDeviceStorage->managerBackends();

    for (QObject *backendObj : backends) {
        Ifaces::DeviceManager *backend = qobject_cast<Ifaces::DeviceManager *>(backendObj);

        if (backend == nullptr) {
            continue;
        }
        if (!backend->supportedInterfaces().contains(type)) {
            continue;
        }

        const QStringList udis = backend->devicesFromQuery(parentUdi, type);
        for (const QString &udi : udis) {
            list.append(Device(udi));
        }
    }

    return list;
}

QList<Solid::Device> Solid::Device::listFromQuery(const Predicate &predicate,
        const QString &parentUdi)
{
    QList<Device> list;
    const QSet<DeviceInterface::Type> usedTypes = predicate.usedTypes();
    const QList<QObject *> backends = globalDeviceStorage->managerBackends();
    for (QObject *backendObj : backends) {
        Ifaces::DeviceManager *backend = qobject_cast<Ifaces::DeviceManager *>(backendObj);

        if (backend == nullptr) {
            continue;
        }

        QStringList udis;
        if (predicate.isValid()) {
            QSet<DeviceInterface::Type> supportedTypes = backend->supportedInterfaces();
            if (supportedTypes.intersect(usedTypes).isEmpty()) {
                continue;
            }

            QList<DeviceInterface::Type> sortedTypes = supportedTypes.values();
            std::sort(sortedTypes.begin(), sortedTypes.end());
            for (DeviceInterface::Type type : qAsConst(sortedTypes)) {
                udis += backend->devicesFromQuery(parentUdi, type);
            }
        } else {
            udis += backend->allDevices();
        }

        QSet<QString> seen;
        for (const QString &udi : qAsConst(udis)) {
            if (seen.contains(udi)) {
                continue;
            }
            seen.insert(udi);
            Device dev(udi);

            bool matches = false;

            if (!predicate.isValid()) {
                matches = true;
            } else {
                matches = predicate.matches(dev);
            }

            if (matches) {
                list.append(dev);
            }
        }
    }

    return list;
}

Solid::Device Solid::Device::storageAccessFromPath(QString path)
{
    QFileInfo fileInfo = QFileInfo(path);
    if (!QFileInfo::exists(path)) {
        qCWarning(DEVICE_MANAGER_LOG) << "Incorrect file path";
        return Device();
    }
    //We ensure file and all mount paths are with trailing dir separators, to avoid false positive matches later
    if (!path.endsWith(QDir::separator())) {
        path.append(QDir::separator());
    }

    QList<Device> list = Solid::Device::listFromType(DeviceInterface::Type::StorageAccess);
    Device match;
    int match_length = 0;
    for (const Device &device : list) {
        StorageAccess *storageAccess = device.as<StorageAccess>();
        QString mountPath = storageAccess->filePath();
        if (!mountPath.endsWith(QDir::separator())) {
            mountPath.append(QDir::separator());
        }
        if (path.startsWith(mountPath) && mountPath.size() > match_length) {
            match_length = mountPath.size();
            match = device;
        }
    }
    return match;
}

Solid::DeviceNotifier *Solid::DeviceNotifier::instance()
{
    return globalDeviceStorage->notifier();
}

void Solid::DeviceManagerPrivate::_k_deviceAdded(const QString &udi)
{
    if (m_devicesMap.contains(udi)) {
        DevicePrivate *dev = m_devicesMap[udi].data();

        // Ok, this one was requested somewhere was invalid
        // and now becomes magically valid!

        if (dev && dev->backendObject() == nullptr) {
            dev->setBackendObject(createBackendObject(udi));
            Q_ASSERT(dev->backendObject() != nullptr);
        }
    }

    emit deviceAdded(udi);
}

void Solid::DeviceManagerPrivate::_k_deviceRemoved(const QString &udi)
{
    if (m_devicesMap.contains(udi)) {
        DevicePrivate *dev = m_devicesMap[udi].data();

        // Ok, this one was requested somewhere was valid
        // and now becomes magically invalid!

        if (dev) {
            Q_ASSERT(dev->backendObject() != nullptr);
            dev->setBackendObject(nullptr);
            Q_ASSERT(dev->backendObject() == nullptr);
        }
    }

    emit deviceRemoved(udi);
}

void Solid::DeviceManagerPrivate::_k_destroyed(QObject *object)
{
    QString udi = m_reverseMap.take(object);

    if (!udi.isEmpty()) {
        m_devicesMap.remove(udi);
    }
}

Solid::DevicePrivate *Solid::DeviceManagerPrivate::findRegisteredDevice(const QString &udi)
{
    if (udi.isEmpty()) {
        return m_nullDevice.data();
    } else if (m_devicesMap.contains(udi)) {
        return m_devicesMap[udi].data();
    } else {
        Ifaces::Device *iface = createBackendObject(udi);

        DevicePrivate *devData = new DevicePrivate(udi);
        devData->setBackendObject(iface);

        QPointer<DevicePrivate> ptr(devData);
        m_devicesMap[udi] = ptr;
        m_reverseMap[devData] = udi;

        connect(devData, SIGNAL(destroyed(QObject*)),
                this, SLOT(_k_destroyed(QObject*)));

        return devData;
    }
}

Solid::Ifaces::Device *Solid::DeviceManagerPrivate::createBackendObject(const QString &udi)
{
    const QList<QObject *> backends = globalDeviceStorage->managerBackends();

    for (QObject *backendObj : backends) {
        Ifaces::DeviceManager *backend = qobject_cast<Ifaces::DeviceManager *>(backendObj);

        if (backend == nullptr) {
            continue;
        }
        if (!udi.startsWith(backend->udiPrefix())) {
            continue;
        }

        Ifaces::Device *iface = nullptr;

        QObject *object = backend->createDevice(udi);
        iface = qobject_cast<Ifaces::Device *>(object);

        if (iface == nullptr) {
            delete object;
        }

        return iface;
    }

    return nullptr;
}

Solid::DeviceManagerStorage::DeviceManagerStorage()
{

}

QList<QObject *> Solid::DeviceManagerStorage::managerBackends()
{
    ensureManagerCreated();
    return m_storage.localData()->managerBackends();
}

Solid::DeviceNotifier *Solid::DeviceManagerStorage::notifier()
{
    ensureManagerCreated();
    return m_storage.localData();
}

void Solid::DeviceManagerStorage::ensureManagerCreated()
{
    if (!m_storage.hasLocalData()) {
        m_storage.setLocalData(new DeviceManagerPrivate());
    }
}

#include "moc_devicemanager_p.cpp"
#include "moc_devicenotifier.cpp"

