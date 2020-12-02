/*
    SPDX-FileCopyrightText: 2010 Mario Bensi <mbensi@ipsquad.net>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "fstabstorageaccess.h"
#include "fstabwatcher.h"
#include <solid/devices/backends/fstab/fstabdevice.h>
#include <solid/devices/backends/fstab/fstabhandling.h>
#include <solid/devices/backends/fstab/fstabservice.h>
#include <QStringList>

#include <QProcess>
#include <QTimer>

#include <errno.h>

#define MTAB "/etc/mtab"

using namespace Solid::Backends::Fstab;

FstabStorageAccess::FstabStorageAccess(Solid::Backends::Fstab::FstabDevice *device) :
    QObject(device),
    m_fstabDevice(device)
{
    QStringList currentMountPoints = FstabHandling::currentMountPoints(device->device());
    if (currentMountPoints.isEmpty()) {
        QStringList mountPoints = FstabHandling::mountPoints(device->device());
        m_filePath = mountPoints.isEmpty() ? QString() : mountPoints.first();
        m_isAccessible = false;
    } else {
        m_filePath = currentMountPoints.first();
        m_isAccessible = true;
    }
    m_isIgnored = FstabHandling::options(device->device()).contains(QLatin1String("x-gvfs-hide"));

    connect(device, SIGNAL(mtabChanged(QString)), this, SLOT(onMtabChanged(QString)));
    QTimer::singleShot(0, this, SLOT(connectDBusSignals()));
}

FstabStorageAccess::~FstabStorageAccess()
{
}

void FstabStorageAccess::connectDBusSignals()
{
    m_fstabDevice->registerAction("setup", this,
                                  SLOT(slotSetupRequested()),
                                  SLOT(slotSetupDone(int,QString)));

    m_fstabDevice->registerAction("teardown", this,
                                  SLOT(slotTeardownRequested()),
                                  SLOT(slotTeardownDone(int,QString)));
}

const Solid::Backends::Fstab::FstabDevice *FstabStorageAccess::fstabDevice() const
{
    return m_fstabDevice;
}

bool FstabStorageAccess::isAccessible() const
{
    return m_isAccessible;
}

QString FstabStorageAccess::filePath() const
{
    return m_filePath;
}

bool FstabStorageAccess::isIgnored() const
{
    return m_isIgnored;
}

bool FstabStorageAccess::setup()
{
    if (filePath().isEmpty()) {
        return false;
    }
    m_fstabDevice->broadcastActionRequested("setup");
    return FstabHandling::callSystemCommand("mount", {filePath()}, this, [this](QProcess *process) {
        if (process->exitCode() == 0) {
            m_fstabDevice->broadcastActionDone("setup", Solid::NoError, QString());
        } else {
            m_fstabDevice->broadcastActionDone("setup", Solid::UnauthorizedOperation, process->readAllStandardError());
        }
    });
}

void FstabStorageAccess::slotSetupRequested()
{
    emit setupRequested(m_fstabDevice->udi());
}

bool FstabStorageAccess::teardown()
{
    if (filePath().isEmpty()) {
        return false;
    }
    m_fstabDevice->broadcastActionRequested("teardown");
    return FstabHandling::callSystemCommand("umount", {filePath()}, this, [this](QProcess *process) {
        if (process->exitCode() == 0) {
            m_fstabDevice->broadcastActionDone("teardown", Solid::NoError);
        } else if (process->exitCode() == EBUSY) {
            m_fstabDevice->broadcastActionDone("teardown", Solid::DeviceBusy);
        } else if (process->exitCode() == EPERM) {
            m_fstabDevice->broadcastActionDone("teardown", Solid::UnauthorizedOperation, process->readAllStandardError());
        } else {
            m_fstabDevice->broadcastActionDone("teardown", Solid::OperationFailed, process->readAllStandardError());
        }
    });
}

void FstabStorageAccess::slotTeardownRequested()
{
    emit teardownRequested(m_fstabDevice->udi());
}

void FstabStorageAccess::slotSetupDone(int error, const QString &errorString)
{
    emit setupDone(static_cast<Solid::ErrorType>(error), errorString, m_fstabDevice->udi());
}

void FstabStorageAccess::slotTeardownDone(int error, const QString &errorString)
{
    emit teardownDone(static_cast<Solid::ErrorType>(error), errorString, m_fstabDevice->udi());
}

void FstabStorageAccess::onMtabChanged(const QString &device)
{
    QStringList currentMountPoints = FstabHandling::currentMountPoints(device);
    if (currentMountPoints.isEmpty()) {
        // device umounted
        m_filePath = FstabHandling::mountPoints(device).first();
        m_isAccessible = false;
        emit accessibilityChanged(false, QString(FSTAB_UDI_PREFIX) + "/" + device);
    } else {
        // device added
        m_filePath = currentMountPoints.first();
        m_isAccessible = true;
        emit accessibilityChanged(true, QString(FSTAB_UDI_PREFIX) + "/" + device);
    }
}
